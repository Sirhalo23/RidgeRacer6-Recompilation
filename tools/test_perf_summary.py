import tempfile
import unittest
from pathlib import Path

from perf_summary import summarize


class PerfSummaryTests(unittest.TestCase):
    def summarize(self, text, **options):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "frames.csv"
            path.write_text(text)
            return summarize(path, **options)

    def test_weighted_fps_and_observed_percentiles(self):
        result = self.summarize("frame_index,elapsed_us,frame_time_us,issue_swap_cpu_us\n"
                                "0,0,0,1\n1,10000,10000,2000\n2,40000,30000,4000\n"
                                "3,140000,100000,5000\n")
        self.assertEqual(result["frames"], 3)
        self.assertEqual(result["zero_intervals_skipped"], 1)
        self.assertAlmostEqual(result["interval_fps"], 3 / .14)
        self.assertEqual(result["frame_time"]["p50_ms"], 30)
        self.assertEqual(result["frame_time"]["p99_ms"], 100)
        self.assertEqual(result["stall_frames"], 1)
        self.assertEqual(result["issue_swap_cpu"]["p50_ms"], 4)
        self.assertEqual(result["counter_totals"], {})

    def test_legacy_window_and_uninstrumented_caches(self):
        result = self.summarize("frame_time_us,pipeline_cache_hits,pipeline_cache_misses\n"
                                "0,0,0\n100000,0,0\n100000,0,0\n100000,0,0\n",
                                skip_seconds=.15, duration=.1)
        self.assertEqual(result["frames"], 1)
        self.assertIsNone(result["cache_hit_rates"]["pipeline"])

    def test_cache_event_totals(self):
        result = self.summarize("frame_time_us,texture_cache_hits,texture_cache_misses\n"
                                "10000,2,1\n10000,1,0\n")
        self.assertEqual(result["cache_hit_rates"]["texture"], .75)

    def test_invalid_input_does_not_silently_bias_results(self):
        for text in ("fps\n60\n", "frame_time_us\n-1\n", "frame_time_us\nnan\n",
                     "frame_time_us,draw_calls\n10000\n", "frame_time_us\n0\n"):
            with self.subTest(text=text), self.assertRaises(ValueError):
                self.summarize(text)

    def test_empty_selected_window(self):
        with self.assertRaisesRegex(ValueError, "no positive"):
            self.summarize("frame_time_us\n10000\n", skip_seconds=1)

    def test_invalid_options(self):
        for options in ({"skip_seconds": -1}, {"duration": 0}, {"target_fps": 0},
                        {"stall_ms": -1}, {"skip_seconds": float("nan")},
                        {"duration": float("inf")}):
            with self.subTest(options=options), self.assertRaises(ValueError):
                self.summarize("frame_time_us\n10000\n", **options)


if __name__ == "__main__":
    unittest.main()
