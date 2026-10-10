import csv
import importlib.util
import json
from pathlib import Path
import tempfile
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/windows'))
from thread_cpu import summarize_threads

spec = importlib.util.spec_from_file_location('profile_game', Path(__file__).resolve().parents[1] / 'tools/windows/profile_game.py')
profile = importlib.util.module_from_spec(spec)
spec.loader.exec_module(profile)


class ProfileSummary(unittest.TestCase):
    def test_phase_boundaries_counters_and_memory(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            (out / 'phases.json').write_text(json.dumps([dict(name='pass_1', start_ns=1000_000_000, end_ns=1200_000_000)]), encoding='utf-8')
            with (out / 'frames.csv').open('w', newline='', encoding='utf-8') as f:
                writer = csv.writer(f)
                writer.writerow(['clock_ns', 'frame_ms', 'image_upload_bytes', 'buffer_upload_bytes', 'draws', 'compiles', 'compile_ns'])
                writer.writerows([
                    [900_000_000, 400, 100, 200, 1000, 50, 500],
                    [1010_000_000, 20, 110, 210, 1010, 51, 510], # crosses phase start
                    [1030_000_000, 20, 120, 220, 1020, 52, 520],
                    [1050_000_000, 20, 130, 230, 1030, 52, 520],
                    [1160_000_000, 110, 140, 240, 1040, 53, 550],
                    [1300_000_000, 140, 999, 999, 9999, 99, 999], # outside phase
                ])
            with (out / 'ram.csv').open('w', newline='', encoding='utf-8') as f:
                writer = csv.writer(f)
                writer.writerow(['clock_ns', 'working_set_bytes', 'private_commit_bytes', 'page_faults'])
                writer.writerows([[900_000_000, 9999, 9999, 0], [1100_000_000, 123, 456, 7]])
            (out / 'game.log').write_text('GPU memory: clock_ns=1100000000 usage=3000 budget=5000 host_usage=400 host_budget=8000 guest_images=900 pressure=4250 critical=4750 evictions=0 downloads=0\n', encoding='utf-8')
            result = profile.summarize(out)['phases'][0]
            self.assertEqual(result['frames'], 3)
            self.assertEqual(result['fps'], 20)
            self.assertEqual(result['median_ms'], 20)
            self.assertEqual(result['p99_ms'], 110)
            self.assertEqual(result['over_100_ms'], 1)
            self.assertIsNone(result['image_upload_bytes'])
            self.assertEqual(result['image_refresh_bytes'], 40)
            self.assertEqual(result['compiles'], 3)
            self.assertEqual(result['compile_ns'], 50)
            self.assertEqual(result['peak_working_set_bytes'], 123)
            self.assertEqual(result['peak_private_commit_bytes'], 456)
            self.assertEqual(result['peak_gpu_usage'], 3000)
            self.assertEqual(result['peak_gpu_guest_images'], 900)
            self.assertIsNone(result['gc_timings'])
            # The first report's maximum window straddles phase start. Only
            # subsequent report windows and in-phase cumulative deltas count.
            memory = 'usage=3000 budget=5000 host_usage=400 guest_images=900 '
            (out / 'game.log').write_text('\n'.join([
                'GPU memory: clock_ns=1020000000 ' + memory +
                'query_count=10 query_ns=100 query_max_ns=999999 scan_count=5 scan_ns=50 scan_max_ns=999999',
                'GPU memory: clock_ns=1100000000 ' + memory +
                'query_count=20 query_ns=220 query_max_ns=25 scan_count=10 scan_ns=100 scan_max_ns=15',
                'GPU memory: clock_ns=1190000000 ' + memory +
                'query_count=30 query_ns=340 query_max_ns=35 scan_count=10 scan_ns=100 scan_max_ns=0',
                'GPU memory: clock_ns=1300000000 ' + memory +
                'query_count=100 query_ns=999999 query_max_ns=999999 scan_count=100 scan_ns=999999 scan_max_ns=999999',
            ]), encoding='utf-8')
            timing = profile.summarize(out)['phases'][0]['gc_timings']
            self.assertEqual(timing, dict(start_ns=1020000000, end_ns=1190000000,
                query_count=20, query_ns=240, query_mean_ns=12, query_max_ns=35,
                scan_count=5, scan_ns=50, scan_mean_ns=10, scan_max_ns=15))
            with (out / 'frames.csv').open(encoding='utf-8', newline='') as f:
                rows = list(csv.DictReader(f))
            with (out / 'frames.csv').open('w', encoding='utf-8', newline='') as f:
                writer = csv.DictWriter(f, fieldnames=[*rows[0], 'image_refresh_bytes'])
                writer.writeheader()
                for row in rows:
                    row['image_refresh_bytes'] = int(row['image_upload_bytes']) * 2
                    writer.writerow(row)
            result = profile.summarize(out)['phases'][0]
            self.assertEqual(result['image_upload_bytes'], 40)
            self.assertEqual(result['image_refresh_bytes'], 80)
            # A clock mismatch must fail instead of publishing a plausible FPS.
            (out / 'phases.json').write_text(json.dumps([dict(name='wrong-clock', start_ns=2000_000_000, end_ns=3000_000_000)]), encoding='utf-8')
            with self.assertRaisesRegex(ValueError, 'No frames'):
                profile.summarize(out)

    def test_thread_cpu_excludes_cross_boundary_samples_and_keeps_late_name(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'thread-cpu.csv'
            with path.open('w', newline='', encoding='utf-8') as stream:
                writer = csv.writer(stream)
                writer.writerow(['clock_ns', 'previous_ns', 'thread_id', 'name', 'kernel_ns', 'user_ns'])
                writer.writerows([
                    [1100, 900, 7, '', 10000, 10000], # straddles phase start
                    [1300, 1100, 7, '', 50, 100],
                    [1600, 1300, 7, 'DrawPrep', 50, 200],
                    [1900, 1600, 8, 'Present', 0, 100],
                    [2100, 1900, 7, 'DrawPrep', 10000, 10000], # straddles end
                ])
            result = summarize_threads(path, 1000, 2000)
            self.assertEqual(result[0], dict(thread_id=7, name='DrawPrep',
                cpu_ms=400 / 1e6, percent_of_one_core=40))
            self.assertEqual(result[1]['thread_id'], 8)
            self.assertEqual(result[1]['percent_of_one_core'], 10)


if __name__ == '__main__':
    unittest.main()
