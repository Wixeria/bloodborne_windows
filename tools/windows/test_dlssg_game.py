"""Run a matched, isolated saved-scene FG experiment; keep the game focused.

Uses a copy of the supplied user directory. BB_PAD_FILE provides deterministic
menu navigation. Results distinguish rendered frames from Streamline's actual
presentation count. A background-window run is reported as invalid evidence.
"""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from patches import read_settings, launch_plan


def hashes(path):
    result={}
    for p in path.rglob('*'):
        if p.is_file():
            with p.open('rb') as f: result[str(p.relative_to(path))]=hashlib.file_digest(f,'sha256').hexdigest()
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode', choices=('on', 'control', 'native'), default='on')
    parser.add_argument('--build', type=Path, default=ROOT / 'out/windows-fg-game')
    parser.add_argument('--source-user', type=Path, default=ROOT / 'out/windows-play-user')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--seconds', type=int, default=30)
    parser.add_argument('--camera-motion', action='store_true',
                        help='Oscillate the copied save camera during measurement')
    parser.add_argument('--output-res', choices=('1280x720','1920x1080','2560x1440','3840x2160'), default='1280x720')
    parser.add_argument('--window-res', choices=('1280x720','1280x960','1920x1080','2560x1440','3840x2160'), default='1280x720',
                        help='1280x960 exercises letterboxed HUDless presentation in a 4:3 window')
    parser.add_argument('--window-mode', choices=('windowed','borderless','fullscreen'), default='windowed')
    parser.add_argument('--preset', type=int, choices=range(5), default=0)
    parser.add_argument('--live-resolution', choices=('0','1','auto'), default='0')
    parser.add_argument('--multiplier', type=int, choices=range(2,7), default=2)
    parser.add_argument('--fps-limit', type=int, default=120)
    parser.add_argument('--warmup', type=int, default=10)
    parser.add_argument('--stop-timeout', type=int, default=90)
    parser.add_argument('--parallel', action='store_true', help='Test explicit asynchronous FG input leases')
    parser.add_argument('--timings', action='store_true', help='Log presentation CPU wait stages')
    parser.add_argument('--reflex-trace', action='store_true', help='Trace guest pad and graphics submission boundaries')
    parser.add_argument('--reflex', choices=('off','on','boost'), default='on')
    parser.add_argument('--memory-model', choices=('old','new'), default='old',
                        help='Compare 0.5 Windows host-import/layer memory against the old model')
    parser.add_argument('--require-reflex', action='store_true')
    parser.add_argument('--require-hudless', action='store_true', help='Reject runs without a tagged pre-UI scene')
    parser.add_argument('--no-reflex-sleep', action='store_true', help='Legacy archived-build diagnostic; current guest pacing is controlled by --reflex')
    parser.add_argument('--early-sleep', action=argparse.BooleanOptionalAction, default=True,
                        help='Control legacy renderer fallback; current guest input pacing follows --reflex')
    args = parser.parse_args()
    case, build, source = args.output.resolve(), args.build.resolve(), args.source_user.resolve()
    if not case.is_relative_to((ROOT / 'out').resolve()) or case.exists():
        raise ValueError('Use a new directory inside workspace out/')
    if not 10 <= args.seconds <= 120:
        raise ValueError('Measurement duration must be 10..120 seconds')
    exe = build / 'bb-game.exe'
    if not exe.is_file():
        raise ValueError('Prepare the game FG runtime first')
    case.mkdir(parents=True)
    before = hashes(source / 'savedata')
    user = case / 'user'
    shutil.copytree(source / 'savedata', user / 'savedata')
    if (source / 'cache').exists():
        shutil.copytree(source / 'cache', user / 'cache')
    config = case / 'bbport.ini'
    config.write_text(f'upscaler=dlss\npreset={args.preset}\noutput_res={args.output_res}\nlive_resolution={args.live_resolution}\n'
                      f'window_mode={args.window_mode}\nfg_multiplier={args.multiplier}\n'
                      'skip_intro=1\neffect_chromatic_aberration=0\neffect_motion_blur=0\nshow_fps=1\n',encoding='utf-8')
    env = {k:v for k,v in os.environ.items() if not k.startswith('BB_')}
    command = ['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',str(ROOT/'run-windows.ps1'),
               '-Gpu','-SkipBuild','-BaseOnly','-BuildDirectory',str(build),
               '-OutputDirectory',str(case),'-UserDirectory',str(user),'-FpsPatch','uncap',
               '-FpsLimit',str(args.fps_limit),'-PrepareOnly']
    with (case / 'prepare.log').open('w') as log:
        subprocess.run(command,cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
    game = json.loads((ROOT/'out/windows-local.json').read_text(encoding='utf-8-sig'))['gameDirectory']
    plan = launch_plan(read_settings(config))
    if plan['render']:
        env.update(BB_RENDER_RES=plan['render'],BB_OUTPUT_RES=plan['output'],BB_DMEM_MB=str(plan['direct_memory_mb']))
    pad, stop = case/'pad.txt', case/'stop.trigger'
    pad.write_text('')
    (case/'capture').mkdir()
    env.update(BB_CONFIG=str(config), BB_GPU_USER_DIR=str(user), BB_FRAME_CSV=str(case/'frames.csv'),
               BB_FRAME_STATS='0',BB_GPU_PROFILE='0',BB_PAD_FILE=str(pad),BB_PAD_SCRIPT_ONLY='1',
               BB_STOP_FILE=str(stop),BB_VBLANK_HZ='0',BB_FPS_LIMIT=str(args.fps_limit),BB_PRESENT_MODE='Immediate',BB_WINDOW_RES=args.window_res,
               BB_WINDOW_MODE=args.window_mode,
               BB_FRAME_GENERATION='off' if args.mode=='native' else args.mode,
               BB_STREAMLINE_DIR=str(build),BB_DLSSG_SM86='1' if (build/'version.dll').exists() else '0',
               BB_DUMP_DIR=str(case/'capture'), BB_PRESENT_DUMP_TRIGGER=str(case/'capture.trigger'),
                BB_DUMP_FG_SCENE_TRIGGER=str(case/'hudless.trigger'),
                BB_DUMP_FG_COLOR_TRIGGER=str(case/'fg-color.trigger'))
    env['BB_DLSSG_PARALLEL'] = '1' if args.parallel else '0'
    env['BB_REFLEX'] = args.reflex
    env['BB_PC_MODEL'] = '1' if args.memory_model == 'new' else '0'
    env['BB_DLSSG_TIMINGS'] = '1' if args.timings else '0'
    env['BB_REFLEX_TRACE'] = '1' if args.reflex_trace else '0'
    env['BB_DLSSG_REFLEX_SLEEP'] = '0' if args.no_reflex_sleep else '1'
    env['BB_DLSSG_EARLY_SLEEP'] = '1' if args.early_sleep and not args.no_reflex_sleep else '0'
    command=[str(exe),str(case/'boot-linked.bin'),'--app0',game,'--user',str(user),
             '--content-profile',str(case/'content.bin'),'--patches',str(case/'patches.bin'),'--timeout','240']
    (case/'session.json').write_text(json.dumps({'mode':args.mode,'camera_motion':args.camera_motion,
                                              'command':command,'plan':plan,
                                              'exe_sha256':hashes(build).get('bb-game.exe'),
                                              'settings':vars(args)},indent=2,default=str))
    failure=None
    with (case/'game.log').open('w') as log, (case/'game-errors.log').open('w') as errors:
        # Separate handles prevent concurrent CRT stdout/stderr writes from
        # splicing warnings into Reflex markers and invalidating their parser.
        proc=subprocess.Popen(command,cwd=build,env=env,stdout=log,stderr=errors)
        print(f'Game PID {proc.pid}, mode={args.mode}. Keep its window focused. Log: {case / "game.log"}',flush=True)
        def text():
            return ((case/'game.log').read_text(errors='replace') + '\n' +
                    (case/'game-errors.log').read_text(errors='replace'))
        def wait(seconds):
            deadline=time.monotonic()+seconds
            while time.monotonic()<deadline:
                if proc.poll() is not None: raise RuntimeError(f'Game exited: {proc.returncode}')
                time.sleep(.1)
        def until(needle,seconds):
            deadline=time.monotonic()+seconds
            while needle not in text():
                if time.monotonic()>deadline: raise TimeoutError(f'Missing: {needle}')
                wait(.5)
        def press():
            pad.write_text('cross');wait(.7);pad.write_text('');wait(1)
        try:
            # Native 1080p menus need no redirected UI target and do not emit the
            # composition message. Wait for real flips rather than a scaling-only log.
            deadline=time.monotonic()+90
            while not (case/'frames.csv').exists() or (case/'frames.csv').stat().st_size < 4096:
                if time.monotonic()>deadline: raise TimeoutError('No steady menu flips')
                wait(.5)
            wait(3)
            press();wait(4);press();wait(4);press()
            until('Camera motion: scene render size',90)
            print('Gameplay reached; warming up.',flush=True)
            wait(args.warmup)
            (case/'capture.trigger').write_text('capture')
            (case/'hudless.trigger').write_text('capture')
            (case/'fg-color.trigger').write_text('capture')
            wait(2)
            phase={'start_ns':time.perf_counter_ns(),'seconds':args.seconds}
            if args.camera_motion:
                deadline=time.monotonic()+args.seconds
                right=True
                while time.monotonic()<deadline:
                    pad.write_text('rx=176' if right else 'rx=80')
                    wait(min(1.5, max(0,deadline-time.monotonic())))
                    right=not right
                pad.write_text('')
            else:
                wait(args.seconds)
            phase['end_ns']=time.perf_counter_ns()
            (case/'phase.json').write_text(json.dumps(phase,indent=2))
        except Exception as error:
            failure=str(error)
        finally:
            pad.write_text('');stop.write_text('stop')
            try: proc.wait(timeout=args.stop_timeout)
            except subprocess.TimeoutExpired:
                proc.terminate();proc.wait(timeout=10)
    if proc.returncode != 0:
        failure = failure or f'Game exited: {proc.returncode}'
    unchanged=before==hashes(source/'savedata')
    records=[{'real_fps':float(a),'presented_fps':float(b),'submitted':int(c),'presentations':int(d),
              'status':int(e),'fg':int(f),'focused':int(g),'unfocused_frames':int(h)}
             for a,b,c,d,e,f,g,h in re.findall(
             r'DLSSG stats: real=([\d.]+) FPS presented=([\d.]+) FPS submitted=(\d+) presentations=(\d+) status=(\d+) fg=(\d+) focused=(\d+) unfocused=(\d+)',text())]
    steady=records[-max(1,args.seconds//2-2):]
    valid=proc.returncode == 0 and bool(steady) and all(
        r['status']==0 and r['focused'] and r['unfocused_frames']==0 and
        r['fg']==(args.mode=='on') and r['real_fps']>0 and
        (args.multiplier * .95 <= r['presented_fps']/r['real_fps'] <= args.multiplier * 1.05 if args.mode=='on'
         else .95 <= r['presented_fps']/r['real_fps'] <= 1.05)
        for r in steady)
    summary={'mode':args.mode,'camera_motion':args.camera_motion,
             'output_res':args.output_res,'window_res':args.window_res,'window_mode':args.window_mode,
             'preset':args.preset,'live_resolution':args.live_resolution,'multiplier':args.multiplier,
             'exit_code':proc.returncode,'failure':failure,'save_unchanged':unchanged,
             'measurement_valid':valid and not failure and unchanged,'records':records,
             'hudless_tagged':'DLSSG: tagged tonemapped HUDless scene' in text(),
             'note':'Guest pad-to-submit markers are instrumented; full engine simulation entry and certified input latency remain unverified.'}
    reflex_records = [dict(zip(('mode','supported','latency_reports','guest_pacing','focused'), map(int,row)))
                      for row in re.findall(r'Reflex stats: mode=(\d) supported=(\d) latency_reports=(\d) guest_pacing=(\d) focused=(\d)', text())]
    summary['reflex_requested'] = args.reflex
    summary['reflex_records'] = reflex_records
    expected_mode = {'off':0,'on':1,'boost':2}[args.reflex]
    if args.mode == 'on' and expected_mode == 0: expected_mode = 1
    summary['reflex_runtime_valid'] = bool(reflex_records) and not failure and unchanged and proc.returncode == 0 and all(
        r['mode'] == expected_mode and r['supported'] and r['guest_pacing'] and r['focused']
        for r in reflex_records[-max(1,args.seconds//2-2):])
    reports = re.findall(r'Reflex report: valid=(\d+) token=(\d+).*? input=(\d+) gpu_active_us=(\d+)', text())
    summary['reflex_driver_reports'] = {'count':len(reports), 'max_valid_frames':max([int(r[0]) for r in reports],default=0),
                                       'nonzero_input_timestamp':any(int(r[2]) for r in reports)}
    if args.reflex_trace:
        events = {}
        for stage,token,epoch,stamp,guest in re.findall(
                r'Reflex marker: stage=(\w+) token=(\d+) epoch=(\d+) clock=(\d+) guest=(\d)', text()):
            if guest == '1' and stage not in ('latency_ping','flash'):
                events.setdefault(int(token), []).append((stage,int(stamp),int(epoch)))
        stages = ['sleep_start','sleep_end','simulation_start','input_sample','simulation_end',
                  'render_start','render_end','present_start','present_end']
        complete = {t: e for t,e in events.items() if e[-1][0] == 'present_end'}
        bad = {t: e for t,e in complete.items() if [s for s,_,_ in e] != stages}
        summary['reflex_trace'] = {'sampled_tokens':len(events), 'presented_tokens':len(complete),
                                  'bad_marker_tokens':len(bad), 'first_bad':list(bad.items())[:3]}
        if bad or not complete:
            failure = summary['failure'] = failure or 'Reflex token marker order is incomplete or duplicated'
            summary['reflex_runtime_valid'] = False
            summary['measurement_valid'] = False
    if args.mode == 'native':
        summary['measurement_valid'] = summary['reflex_runtime_valid']
    if args.require_reflex and not summary['reflex_runtime_valid']:
        failure = summary['failure'] = failure or 'Reflex support, requested mode, guest pacing or focus check failed'
        summary['measurement_valid'] = False
    tagged = re.findall(r'DLSSG: tagged tonemapped HUDless scene (\d+x\d+)', text())
    outputs = re.findall(r'DLSSG: gameplay=1 generation=\d input=\d+x\d+ output=(\d+x\d+)', text())
    summary['hudless_tagged_resolutions'] = tagged
    summary['fg_output_resolutions'] = outputs
    summary['hudless_presentation_blits'] = re.findall(
        r'DLSSG: HUDless presentation blit (\d+x\d+) -> (\d+x\d+)', text())
    summary['presentation_regions'] = re.findall(
        r'DLSSG: presentation region (\d+),(\d+) (\d+x\d+)', text())
    if outputs and summary['presentation_regions']:
        w,h = map(int, outputs[-1].split('x'))
        sw,sh = map(int, args.output_res.split('x'))
        rw,rh = (w,w*sh//sw) if w*sh <= h*sw else (h*sw//sh,h)
        expected_region = [str((w-rw)//2),str((h-rh)//2),f'{rw}x{rh}']
        summary['expected_presentation_region'] = expected_region
        if list(summary['presentation_regions'][-1]) != expected_region:
            failure = summary['failure'] = failure or 'FG presentation rectangle does not match the window letterboxing'
            summary['measurement_valid'] = valid = False
    if args.require_hudless and not summary['hudless_tagged']:
        failure = summary['failure'] = failure or 'No tonemapped HUDless scene was tagged'
        summary['measurement_valid'] = valid = False
    elif args.require_hudless and (not outputs or tagged[-1] != outputs[-1]):
        failure = summary['failure'] = failure or 'HUDless tag does not match the FG output dimensions'
        summary['measurement_valid'] = valid = False
    summary['parallel_requested'] = args.parallel
    summary['parallel_fallback'] = 'input completion timeline unavailable' in text()
    timings = re.findall(r'DLSSG CPU: begin=([\d.]+) acquire=([\d.]+) submit_lock=([\d.]+) present=([\d.]+) ms fg=(\d)', text())
    selected = [[float(x) for x in row[:4]] for row in timings if int(row[4]) == (args.mode == 'on')]
    if selected:
        summary['cpu_stage_mean_ms'] = dict(zip(('begin','acquire','submit_lock','present'),
                                              [sum(x)/len(x) for x in zip(*selected)]))
    if (case/'phase.json').exists() and (case/'frames.csv').exists():
        with (case/'frames.csv').open(newline='') as f:
            rows=[r for r in csv.DictReader(f) if phase['start_ns'] <= int(r['clock_ns']) <= phase['end_ns']
                  and int(r['clock_ns'])-float(r['frame_ms'])*1e6 >= phase['start_ns'] and float(r['frame_ms'])>0]
        ms=sorted(float(r['frame_ms']) for r in rows)
        if ms:
            summary['guest_flip_fps']=1000*len(ms)/sum(ms)
            summary['guest_p99_ms']=ms[max(0,int(len(ms)*.99)-1)]
            summary['guest_measured_frames']=len(ms)
    # Failed/loading-only runs must not report menu FPS as gameplay performance.
    if steady and summary['measurement_valid'] and (case/'phase.json').exists():
        summary['steady_mean_real_fps']=sum(r['real_fps'] for r in steady)/len(steady)
        summary['steady_mean_presented_fps']=sum(r['presented_fps'] for r in steady)/len(steady)
        summary['presentation_ratio']=sum(r['presented_fps'] for r in steady)/sum(r['real_fps'] for r in steady)
    (case/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
    if (build/'logs/sl.log').exists(): shutil.copy2(build/'logs/sl.log',case/'streamline.log')
    print(json.dumps({k:v for k,v in summary.items() if k not in ('records','reflex_records')},indent=2),flush=True)
    if not unchanged: raise RuntimeError('Source save changed during test')
    return 1 if failure or (args.mode!='native' and not valid) else 0


if __name__=='__main__':
    raise SystemExit(main())
