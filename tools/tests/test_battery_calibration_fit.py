import csv
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from fit_battery_calibration import FIELDS, decreasing_fit, fit_report, interpolate


def synthetic_report(change=None, end_reason="low_voltage"):
    """Explicitly synthetic two-hour fixed-load discharge, never hardware evidence."""
    output = io.StringIO()
    output.write("# nMedia battery calibration v1\n# end_reason=" + end_reason + "\n")
    output.write("# low_endpoint_mv=3500\n# attempts=1441\n# valid_attempts=1441\n# rejected=0\n# rows=241\n")
    writer = csv.DictWriter(output, fieldnames=FIELDS, lineterminator="\n")
    writer.writeheader()
    for i in range(241):
        at = i * 30000
        r = dict.fromkeys(FIELDS, 0)
        r.update(elapsed_ms=at, rtc=1000+at//1000, mv=4150-(660*i)//240,
                 cached_mv=4000, raw=950-i, os_percent=100, charge_mode=1,
                 valid=1, restored=1, cache_unchanged=1, flags=13, cpu_mhz=396,
                 volume=45, codec=7, rate=48000, channels=2, stable_ms=at,
                 playing_ms=at)
        if change:
            change(i, r)
        writer.writerow(r)
    return output.getvalue()


class FitTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.path = Path(self.directory.name) / "synthetic.csv.tns"

    def tearDown(self):
        self.directory.cleanup()

    def write(self, **kwargs):
        self.path.write_text(synthetic_report(**kwargs), encoding="utf-8")

    def test_complete_curve_is_bounded_and_explicit_about_quantity(self):
        self.write()
        result = fit_report(self.path, True)
        self.assertEqual(result["reserve_mv"], 3500)
        self.assertIn("usable playback runtime", result["quantity"])
        self.assertEqual(result["status"], "experimental_requires_independent_run")
        self.assertLess(result["quality"]["same_run_holdout_max_error_points"], 1)
        points = result["points"]
        self.assertEqual(points[0]["percent"], 0)
        self.assertEqual(points[-1]["percent"], 100)
        self.assertTrue(all(a["mv"] < b["mv"] and a["percent"] <= b["percent"]
                            for a, b in zip(points, points[1:])))
        self.assertGreater(interpolate(points, 3800), 40)
        self.assertLess(interpolate(points, 3800), 60)
        json.dumps(result, allow_nan=False)

    def test_full_confirmation_and_complete_endpoint_are_required(self):
        self.write()
        with self.assertRaisesRegex(ValueError, "fully charged"):
            fit_report(self.path)
        self.write(end_reason="manual_exit")
        with self.assertRaisesRegex(ValueError, "partial evidence"):
            fit_report(self.path, True)

    def test_assumed_top_remains_explicit_and_settling_is_bounded(self):
        self.write()
        model = fit_report(self.path, near_full_assumed=True, settle_seconds=10)
        self.assertFalse(model["full_charge_user_confirmed"])
        self.assertTrue(model["near_full_upper_endpoint_assumed"])
        self.assertEqual(model["settle_seconds"], 10)
        with self.assertRaisesRegex(ValueError, "not both"):
            fit_report(self.path, True, near_full_assumed=True)
        for settle in (-1, 0, 1000):
            with self.assertRaisesRegex(ValueError, "Settling"):
                fit_report(self.path, True, settle_seconds=settle)

    def test_charging_pauses_sampling_gaps_and_load_changes_reject(self):
        self.write(change=lambda i, r: r.update(charge_mode=2) if i == 10 else None)
        with self.assertRaisesRegex(ValueError, "charging"):
            fit_report(self.path, True)
        self.write(change=lambda i, r: r.update(cpu_mhz=456) if i > 120 else None)
        with self.assertRaisesRegex(ValueError, "workload changed"):
            fit_report(self.path, True)
        self.write(change=lambda i, r: r.update(flags=15) if 100 <= i <= 110 else None)
        with self.assertRaisesRegex(ValueError, "sampling gap"):
            fit_report(self.path, True)
        self.write(change=lambda i, r: r.update(playing_ms=r["playing_ms"]//2))
        with self.assertRaisesRegex(ValueError, "nonplaying time"):
            fit_report(self.path, True)

    def test_short_flat_and_malformed_runs_reject(self):
        self.write(change=lambda i, r: r.update(elapsed_ms=r["elapsed_ms"]//10,
                                               playing_ms=r["playing_ms"]//10))
        with self.assertRaisesRegex(ValueError, "too short"):
            fit_report(self.path, True)
        self.write(change=lambda i, r: r.update(mv=3500))
        with self.assertRaisesRegex(ValueError, "voltage range"):
            fit_report(self.path, True)
        self.write(change=lambda i, r: r.update(elapsed_ms=0) if i == 2 else None)
        with self.assertRaisesRegex(ValueError, "increase strictly"):
            fit_report(self.path, True)

    def test_voltage_rebounds_are_pooled_and_plateaus_keep_uncertainty(self):
        self.assertEqual(decreasing_fit([4100, 4000, 4020, 3900]), [4100, 4010, 4010, 3900])
        def plateau(i, r):
            if 100 <= i <= 110:
                r["mv"] = 3870
        self.write(change=plateau)
        model = fit_report(self.path, True)
        self.assertGreater(model["quality"]["widest_voltage_plateau_points"], 3)


if __name__ == "__main__":
    unittest.main()
