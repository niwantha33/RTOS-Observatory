# SPDX-License-Identifier: Apache-2.0
import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "adapters"))
from zew_offline_reports import convert


class ConversionTests(unittest.TestCase):
    def cpu(self):
        return {"schema_version": 1, "kind": "cpu", "status": "ok", "incomplete": False,
                "start_ns": 100000, "end_ns": 1000000,
                "cpu": {"thread_states": [
                    {"name": "Task A", "state": "Scheduled", "start_ns": 200000, "end_ns": 500000},
                    {"name": "Task B", "state": "Blocked", "start_ns": 500000, "end_ns": 700000}]}}

    def test_import_preserves_scheduled_semantics(self):
        report = convert(self.cpu())
        self.assertTrue(report["complete"])
        self.assertEqual(report["intervals"][0]["state"], "scheduled")
        self.assertEqual(report["intervals"][0]["start_us"], 200)
        self.assertEqual(report["threads"][0]["priority"], None)

    def test_import_summary_is_not_fake_timestamped_wait(self):
        findings = {"schema_version": 1, "kind": "contention", "status": "ok", "incomplete": False,
                    "findings": [{"kind": "priority_inversion", "waiter": "Task A", "object": "M1",
                                  "holder": "Task C", "holder_unknown": False,
                                  "duration_ns": 321000, "explanation": "Observed inversion pattern"}]}
        report = convert(self.cpu(), findings)
        self.assertFalse(report["waits"])
        self.assertEqual(report["findings"][0]["start_us"], 0)
        self.assertEqual(report["findings"][0]["observed_us"], 321)
        self.assertEqual(report["findings"][0]["confidence"], "imported-aggregate")

    def test_invalid_inputs_fail(self):
        cpu = self.cpu()
        cpu["cpu"]["thread_states"][0]["end_ns"] = 200
        report = convert(cpu)
        self.assertFalse(report["complete"])
        with self.assertRaises(ValueError):
            convert({})


if __name__ == "__main__":
    unittest.main()
