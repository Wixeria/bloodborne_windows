"""Profile a prepared Windows game using an independent copy of a save.

Start the game, select Play Offline/Continue using output/pad.txt, inspect the
scene, then create output/ready.trigger. Only subsequent route passes count.
Keyboard/gamepad input is excluded. RAM counters are working set/private commit;
GPU heap usage/budget and logical guest image bytes are distinct measurements.
"""
import argparse
import csv
import ctypes
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import time


def hashes(directory):
    return {p.relative_to(directory).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(directory.rglob('*')) if p.is_file()}


def percentile(values, fraction):
    values = sorted(values)
    return values[max(0, math.ceil(len(values) * fraction) - 1)]


def summarize(directory):
    directory = Path(directory)
    phases = json.loads((directory / 'phases.json').read_text(encoding='utf-8'))
    with (directory / 'frames.csv').open(encoding='utf-8', newline='') as f:
        frames = [{k: float(v) if k == 'frame_ms' else int(v) for k, v in r.items()}
                  for r in csv.DictReader(f)]
    with (directory / 'ram.csv').open(encoding='utf-8', newline='') as f:
        ram = [{k: int(v) for k, v in r.items()} for r in csv.DictReader(f)]
    gpu = []
    log = (directory / 'game.log').read_text(encoding='utf-8', errors='replace')
    for line in log.splitlines():
        if line.startswith('GPU memory: '):
            gpu.append({k: int(v) for k, v in (t.split('=') for t in line[12:].split())})
    results = []
    for phase in phases:
        start, end = phase['start_ns'], phase['end_ns']
        # Exclude the flip interval that straddles the start boundary.
        selected = [r for r in frames if start <= r['clock_ns'] <= end and
                    r['clock_ns'] - r['frame_ms'] * 1e6 >= start and r['frame_ms'] > 0]
        if not selected:
            raise ValueError('No frames in a route phase; check the clock and game log')
        previous = [r for r in frames if r['clock_ns'] < start]
        if not previous:
            raise ValueError('Missing pre-route frame counters')
        first, last = previous[-1], selected[-1]
        ms = [r['frame_ms'] for r in selected]
        measured_ram = [r for r in ram if start <= r['clock_ns'] <= end]
        measured_gpu = [r for r in gpu if start <= r['clock_ns'] <= end]
        result = dict(name=phase['name'], frames=len(ms), fps=1000 * len(ms) / sum(ms),
                      median_ms=percentile(ms, .5), p95_ms=percentile(ms, .95),
                      p99_ms=percentile(ms, .99), max_ms=max(ms),
                      over_33_ms=sum(v > 33.333333 for v in ms),
                      over_50_ms=sum(v > 50 for v in ms), over_100_ms=sum(v > 100 for v in ms))
        for counter in ('buffer_upload_bytes', 'draws', 'compiles', 'compile_ns'):
            result[counter] = last[counter] - first[counter]
        if 'image_refresh_bytes' in last:
            result['image_refresh_bytes'] = last['image_refresh_bytes'] - first['image_refresh_bytes']
            result['image_upload_bytes'] = last['image_upload_bytes'] - first['image_upload_bytes']
        else:
            # Older binaries counted skipped refreshes as uploads. Preserve the
            # old counter under its correct name, never present it as transfers.
            result['image_refresh_bytes'] = last['image_upload_bytes'] - first['image_upload_bytes']
            result['image_upload_bytes'] = None
        for counter in ('working_set_bytes', 'private_commit_bytes'):
            result['peak_' + counter] = max((r[counter] for r in measured_ram), default=None)
        for counter in ('usage', 'budget', 'host_usage', 'guest_images'):
            result['peak_gpu_' + counter] = max((r[counter] for r in measured_gpu), default=None)
        # Use two in-phase reports so cumulative deltas and maximum windows
        # exclude time before the phase. Older binaries have no timing fields.
        timed_gpu = [r for r in measured_gpu if all(k in r for k in
                     ('query_count', 'query_ns', 'query_max_ns',
                      'scan_count', 'scan_ns', 'scan_max_ns'))]
        result['gc_timings'] = None
        if len(timed_gpu) >= 2:
            timing = dict(start_ns=timed_gpu[0]['clock_ns'], end_ns=timed_gpu[-1]['clock_ns'])
            for kind in ('query', 'scan'):
                count = timed_gpu[-1][kind + '_count'] - timed_gpu[0][kind + '_count']
                elapsed = timed_gpu[-1][kind + '_ns'] - timed_gpu[0][kind + '_ns']
                timing[kind + '_count'] = count
                timing[kind + '_ns'] = elapsed
                timing[kind + '_mean_ns'] = elapsed / count if count else None
                timing[kind + '_max_ns'] = max(r[kind + '_max_ns'] for r in timed_gpu[1:])
            result['gc_timings'] = timing
        results.append(result)
        if (directory / 'thread-cpu.csv').is_file():
            from thread_cpu import summarize_threads
            result['thread_cpu'] = summarize_threads(directory / 'thread-cpu.csv', start, end)
    summary = dict(phases=results,
                   fatal_lines=[l for l in log.splitlines() if l.startswith(('FATAL:', 'UNIMPLEMENTED:', 'STOP:'))],
                   note='Guest flip intervals; GPU heap usage includes buffers and driver allocations. '
                        'Private commit excludes shared guest mappings; working set includes resident pages. '
                        'Image upload bytes are queued guest mip ranges, not measured PCIe traffic; '
                        'older CSV schemas contain only refresh-range bytes. '
                        'GC timings cover the interval between in-phase memory reports; '
                        'RAM and GPU peaks are sampled, not continuous maxima.')
    (directory / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
    return summary


class MemorySampler:
    def __init__(self, pid):
        from ctypes import wintypes
        class Counters(ctypes.Structure):
            _fields_ = [('cb', wintypes.DWORD), ('faults', wintypes.DWORD)] + [
                (name, ctypes.c_size_t) for name in ('peak_ws', 'ws', 'peak_paged', 'paged',
                'peak_nonpaged', 'nonpaged', 'pagefile', 'peak_pagefile', 'private')]
        self.counters = Counters()
        self.counters.cb = ctypes.sizeof(self.counters)
        self.kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        self.kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        self.kernel.OpenProcess.restype = wintypes.HANDLE
        self.kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        self.handle = self.kernel.OpenProcess(0x410, False, pid)
        if not self.handle:
            raise ctypes.WinError(ctypes.get_last_error())
        self.read = ctypes.WinDLL('psapi', use_last_error=True).GetProcessMemoryInfo
        self.read.argtypes = [wintypes.HANDLE, ctypes.POINTER(Counters), wintypes.DWORD]
        self.read.restype = wintypes.BOOL

    def sample(self):
        if not self.read(self.handle, ctypes.byref(self.counters), ctypes.sizeof(self.counters)):
            raise ctypes.WinError(ctypes.get_last_error())
        return [time.perf_counter_ns(), self.counters.ws, self.counters.private, self.counters.faults]

    def close(self):
        self.kernel.CloseHandle(self.handle)


def run(args):
    if os.name != 'nt':
        raise ValueError('Launching this profiler requires Windows; --summarize works elsewhere')
    source = args.source_save.resolve(strict=True)
    cache_source = args.pipeline_cache.resolve(strict=True) if args.pipeline_cache else None
    prepared, executable = args.prepared.resolve(strict=True), args.exe.resolve(strict=True)
    output = args.output.resolve()
    if output.is_relative_to(source) or source.is_relative_to(output):
        raise ValueError('Output and source save must be separate directories')
    if cache_source and (not cache_source.is_dir() or output.is_relative_to(cache_source) or
                         cache_source.is_relative_to(output)):
        raise ValueError('Pipeline cache must be a separate source directory')
    output.mkdir(parents=True, exist_ok=False)
    before = hashes(source)
    (output / 'source-hashes.json').write_text(json.dumps(dict(source=str(source), files=before), indent=2), encoding='utf-8')
    user = output / 'user'
    shutil.copytree(source, user / 'savedata/1/CUSA00207/SPRJ0005')
    cache_before = hashes(cache_source) if cache_source else None
    if cache_source:
        shutil.copytree(cache_source, user / 'cache')
        (output / 'cache-source-hashes.json').write_text(json.dumps(
            dict(source=str(cache_source), files=cache_before), indent=2), encoding='utf-8')
    capture = output / 'capture'
    capture.mkdir()
    pad = output / 'pad.txt'
    pad.write_text('', encoding='ascii')
    mount_manifests = list((prepared / 'mounts').glob('update-*.json'))
    if len(mount_manifests) != 1:
        raise ValueError('Expected exactly one prepared update mount')
    mount = json.loads(mount_manifests[0].read_text(encoding='utf-8'))['mount']
    route = json.loads(args.route.read_text(encoding='utf-8'))
    if not isinstance(route, list) or not route or any(not isinstance(s, dict) or
                        not isinstance(s.get('seconds'), (int, float)) or
                        not 0 < s['seconds'] <= 60 or not isinstance(s.get('pad'), str) for s in route):
        raise ValueError('Each route step needs pad text and 0 < seconds <= 60')
    duration = args.ready_timeout + args.passes * sum(s['seconds'] for s in route) + 20
    env = os.environ.copy()
    for name in ('BB_PAD_RECORD', 'BB_PAD_REPLAY', 'BB_IME_TEXT', 'BB_GC_BUDGET_MB'):
        env.pop(name, None)
    env.update(BB_GPU_USER_DIR=str(user), BB_CONFIG=str(prepared / 'diagnostic-settings.ini'),
               BB_UPSCALER=args.upscaler,
               BB_UPSCALE_PRESET=str(('native', 'quality', 'balanced', 'performance', 'ultra').index(args.upscale_preset)),
               BB_FRAME_STATS='0' if args.minimal_diagnostics else '1', BB_MEMORY_STATS='1',
               BB_SHOW_FPS='1' if args.show_fps else '0',
               BB_FRAME_CSV=str(output / 'frames.csv'), BB_PAD_SCRIPT_ONLY='1', BB_PAD_FILE=str(pad),
               BB_STOP_FILE=str(output / 'stop.trigger'),
               BB_GPU_PROFILE='1' if args.gpu_timing else '0',
               BB_DUMP_DIR=str(capture), BB_PRESENT_DUMP_TRIGGER=str(output / 'capture.trigger'),
               BB_GC_CURRENT_BUDGET='1' if args.gc_policy == 'current' else '0')
    if args.gc_budget_mb is not None:
        env['BB_GC_BUDGET_MB'] = str(args.gc_budget_mb)
    if args.vblank_hz is not None:
        env['BB_VBLANK_HZ'] = str(args.vblank_hz)
    if args.fps_limit is not None:
        env['BB_FPS_LIMIT'] = str(args.fps_limit)
    if args.prep_workers is not None:
        env['BB_PREP_WORKERS'] = str(args.prep_workers)
    if args.pipe_spin_us is not None:
        env['BB_PIPE_SPIN_US'] = str(args.pipe_spin_us)
    if args.optimization_masks:
        env['BB_TOGGLE_FILE'] = str(output / 'optimizations.txt')
    command = [str(executable), str(prepared / 'boot-linked.bin'), '--app0', mount,
               '--user', str(user), '--content-profile', str(prepared / 'content.bin'),
               '--patches', str(prepared / 'patches.bin'), '--timeout', str(math.ceil(duration))]
    phases, sampler, thread_sampler = [], None, None
    with (output / 'game.log').open('w', encoding='utf-8') as log, (output / 'ram.csv').open('w', newline='', encoding='utf-8') as ram:
        process = subprocess.Popen(command, env=env, stdout=log, stderr=subprocess.STDOUT)
        (output / 'session.json').write_text(json.dumps(dict(pid=process.pid, command=command,
            gc_policy=args.gc_policy, route=route, exclusive_input=True,
            gpu_timing=args.gpu_timing, gc_budget_mb=args.gc_budget_mb,
            pipeline_cache_source=str(cache_source) if cache_source else None,
            pacing=dict(vblank_hz=env.get('BB_VBLANK_HZ', '60'),
                        fps_limit=env.get('BB_FPS_LIMIT', 'auto')),
            optimization_masks=args.optimization_masks,
            frame_diagnostics=not args.minimal_diagnostics,
            fps_counter=args.show_fps,
            cpu_thread_sampling=args.cpu_threads,
            helpers=dict(prep_workers=env.get('BB_PREP_WORKERS', 'auto'),
                         pipe_spin_us=env.get('BB_PIPE_SPIN_US', 'auto')),
            rendering=dict(upscaler=args.upscaler, preset=args.upscale_preset)), indent=2), encoding='utf-8')
        print(f'Game PID {process.pid}; inspect scene, then create {output / "ready.trigger"}', flush=True)
        writer = csv.writer(ram)
        writer.writerow(['clock_ns', 'working_set_bytes', 'private_commit_bytes', 'page_faults'])
        try:
            sampler = MemorySampler(process.pid)
            if args.cpu_threads:
                from thread_cpu import ThreadSampler
                thread_sampler = ThreadSampler(process.pid, output / 'thread-cpu.csv')
            next_sample = 0
            def tick():
                nonlocal next_sample
                if process.poll() is not None:
                    raise RuntimeError(f'Game exited during profiling ({process.returncode})')
                now = time.perf_counter()
                if now >= next_sample:
                    writer.writerow(sampler.sample()); ram.flush(); next_sample = now + .5
                    if thread_sampler:
                        thread_sampler.sample()
                time.sleep(.02)
            deadline = time.perf_counter() + args.ready_timeout
            while not (output / 'ready.trigger').is_file():
                if time.perf_counter() >= deadline:
                    raise TimeoutError('Scene was not marked ready')
                tick()
            pad.write_text('', encoding='ascii')
            if not (output / 'frames.csv').is_file():
                raise RuntimeError('Frame CSV missing; use a profiling-enabled binary and check game.log')
            for n in range(args.passes):
                if args.optimization_masks:
                    (output / 'optimizations.txt').write_text(str(args.optimization_masks[n]), encoding='ascii')
                    # The existing runtime watcher polls every 250 ms. Keep its
                    # handoff outside the measured phase, with pad input cleared.
                    deadline = time.perf_counter() + .5
                    while time.perf_counter() < deadline:
                        tick()
                phase = dict(name=f'pass_{n + 1}', start_ns=time.perf_counter_ns())
                if args.optimization_masks:
                    phase['disabled_optimization_mask'] = args.optimization_masks[n]
                print(f'Start {phase["name"]}', flush=True)
                for step in route:
                    pad.write_text(step['pad'], encoding='ascii')
                    deadline = time.perf_counter() + step['seconds']
                    while time.perf_counter() < deadline:
                        tick()
                pad.write_text('', encoding='ascii')
                phase['end_ns'] = time.perf_counter_ns(); phases.append(phase)
                (output / 'phases.json').write_text(json.dumps(phases, indent=2), encoding='utf-8')
            # Let buffered frame rows flush. Only the independent measurement
            # copy is mounted; the diagnostic stop is not an in-game save/quit.
            deadline = time.perf_counter() + 2
            while time.perf_counter() < deadline:
                tick()
        finally:
            pad.write_text('', encoding='ascii')
            if sampler:
                sampler.close()
            if thread_sampler:
                thread_sampler.close()
            unchanged = hashes(source) == before
            (output / 'source-verification.json').write_text(json.dumps(dict(unchanged=unchanged)), encoding='utf-8')
            cache_unchanged = not cache_source or hashes(cache_source) == cache_before
            if cache_source:
                (output / 'cache-source-verification.json').write_text(
                    json.dumps(dict(unchanged=cache_unchanged)), encoding='utf-8')
            stop_start = time.perf_counter()
            try:
                if process.poll() is None:
                    (output / 'stop.trigger').write_text('stop', encoding='ascii')
                process.wait(timeout=15)
            except subprocess.TimeoutExpired:
                print('Diagnostic stop requested; waiting for Windows driver teardown...', flush=True)
                try:
                    process.wait(timeout=75)
                except subprocess.TimeoutExpired:
                    process.terminate()
                    raise RuntimeError(f'Game PID {process.pid} has not completed Windows driver teardown; '
                                       'raw measurements and save verification remain in the output directory')
            (output / 'termination.json').write_text(json.dumps(dict(returncode=process.returncode,
                wait_seconds=time.perf_counter() - stop_start, diagnostic_stop=True)), encoding='utf-8')
            if process.returncode:
                raise RuntimeError(f'Game exited with diagnostic status {process.returncode}')
            if not unchanged:
                raise RuntimeError('Source save changed externally during the test')
            if not cache_unchanged:
                raise RuntimeError('Source pipeline cache changed externally during the test')
    summary = summarize(output)
    for phase in summary['phases']:
        print(f'{phase["name"]}: {phase["fps"]:.1f} FPS, p99 {phase["p99_ms"]:.1f} ms, '
              f'{phase["compiles"]} compiles, {phase["over_50_ms"]} frames >50 ms', flush=True)


def main():
    root = Path(__file__).resolve().parents[2]
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--summarize', type=Path)
    p.add_argument('--exe', type=Path, default=root / 'out/windows/bb-game.exe')
    p.add_argument('--prepared', type=Path, default=root / 'out/windows-game-109')
    p.add_argument('--source-save', type=Path)
    p.add_argument('--pipeline-cache', type=Path,
                   help='Copy an existing user/cache directory for warm-cache validation; source is not mounted')
    p.add_argument('--output', type=Path)
    p.add_argument('--route', type=Path, default=Path(__file__).with_name('camera-route.json'))
    p.add_argument('--passes', type=int, choices=range(1, 11), default=3)
    p.add_argument('--vblank-hz', type=int, help='Override BB_VBLANK_HZ; 0 selects the existing 480 Hz mode')
    p.add_argument('--fps-limit', type=int, help='Override BB_FPS_LIMIT; 0 disables the present-thread limit')
    p.add_argument('--minimal-diagnostics', action='store_true',
                   help='Keep flip CSV and memory samples, disable detailed per-operation/stall diagnostics')
    p.add_argument('--show-fps', action='store_true', help='Show the existing top-right FPS/frame-time overlay')
    p.add_argument('--prep-workers', type=int, choices=range(0, 17),
                   help='Override existing draw preparation worker count (0 disables workers)')
    p.add_argument('--pipe-spin-us', type=int,
                   help='Override existing draw-recorder idle spin duration, in microseconds')
    p.add_argument('--upscaler', choices=('off', 'fsr3', 'dlss'), default='off')
    p.add_argument('--upscale-preset', choices=('native', 'quality', 'balanced', 'performance', 'ultra'),
                   default='quality', help='Upscaler render scale; off keeps native rendering')
    p.add_argument('--optimization-masks', type=lambda v: int(v, 0), nargs='+',
                   help='Disabled optimization mask per pass (one value per pass); experimental A/B control')
    p.add_argument('--ready-timeout', type=int, default=300)
    p.add_argument('--gc-policy', choices=('legacy', 'current'), default='current')
    p.add_argument('--gpu-timing', action='store_true',
                   help='Enable detailed Vulkan timestamps; changes command recording and adds overhead')
    p.add_argument('--cpu-threads', action='store_true',
                   help='Sample Windows thread execution times; diagnostic overhead, no call stacks or suspension')
    p.add_argument('--gc-budget-mb', type=int,
                   help='Simulate a fixed GC budget in MiB; does not reserve or limit actual VRAM')
    args = p.parse_args()
    if args.pipe_spin_us is not None and not 0 <= args.pipe_spin_us <= 10000:
        p.error('Draw-recorder idle spin must be 0..10000 microseconds')
    if ((args.vblank_hz is not None and not 0 <= args.vblank_hz <= 1000) or
        (args.fps_limit is not None and not 0 <= args.fps_limit <= 120)):
        p.error('Vblank override must be 0..1000 Hz and frame limit 0..120 FPS')
    if args.optimization_masks and (len(args.optimization_masks) != args.passes or
                                  any(not 0 <= v < 2**64 for v in args.optimization_masks)):
        p.error('Provide one unsigned 64-bit optimization mask per pass')
    if args.summarize:
        print(json.dumps(summarize(args.summarize), indent=2))
    elif (args.source_save and args.output and args.ready_timeout > 0 and
          (args.gc_budget_mb is None or 0 < args.gc_budget_mb <= 1048576)):
        run(args)
    else:
        p.error('Provide --source-save and a new --output directory, or --summarize; '
                'timeouts and any GC budget must be positive')


if __name__ == '__main__':
    main()
