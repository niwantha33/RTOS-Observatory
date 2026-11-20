"""Controlled regression checks for the baseline-aware quality gate."""
import importlib.util
from pathlib import Path
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "tools" / "quality_gate.py"
SPEC = importlib.util.spec_from_file_location("quality_gate", SCRIPT)
quality_gate = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(quality_gate)


class QualityGateTests(unittest.TestCase):
    def setUp(self):
        self.old = quality_gate.finding("clang-tidy", "src/engine.cpp",
                                        "warning", "bugprone-example", "Existing finding")

    def test_approved_baseline_passes(self):
        errors, warnings = quality_gate.compare_findings([self.old], [self.old])
        self.assertEqual((errors, warnings), ([], []))

    def test_new_error_fails_gate(self):
        fresh = quality_gate.finding("clang-tidy", "src/engine.cpp",
                                     "error", "clang-analyzer-core.NullDereference",
                                     "Potential null dereference")
        errors, warnings = quality_gate.compare_findings([self.old], [self.old, fresh])
        self.assertEqual(errors, [fresh])
        self.assertEqual(warnings, [])

    def test_new_warning_is_advisory(self):
        fresh = quality_gate.finding("cppcheck", "src/engine.cpp",
                                     "warning", "performance", "Potential inefficiency")
        errors, warnings = quality_gate.compare_findings([self.old], [self.old, fresh])
        self.assertEqual(errors, [])
        self.assertEqual(warnings, [fresh])

    def test_duplicate_added_finding_is_detected(self):
        errors, warnings = quality_gate.compare_findings([self.old],
                                                           [self.old, self.old])
        self.assertEqual(errors, [])
        self.assertEqual(warnings, [self.old])

    def test_third_party_and_generated_are_excluded(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for subdir, filename in (
                ("src", "a.cpp"), ("src/generated", "auto.cpp"),
                ("third_party", "vendor.cpp"), ("tests", "a.cpp")
            ):
                location = root / subdir / filename
                location.parent.mkdir(parents=True, exist_ok=True)
                location.write_text("int main() {return 0;}\n")
            observed = [str(path.relative_to(root))
                        for path in quality_gate.owned_files(root)]
            self.assertEqual(observed, ["src/a.cpp", "tests/a.cpp"])
            self.assertIsNone(quality_gate.normalized_path(
                root, str(root / "third_party" / "vendor.cpp")))

    def test_line_number_changes_do_not_change_fingerprint(self):
        self.assertEqual(quality_gate.fingerprint(self.old),
                         quality_gate.fingerprint(dict(self.old)))

    def test_compiler_warning_parsing(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            text = f"{root}/src/a.cpp:12:4: warning: conversion warning [-Wconversion]"
            results = quality_gate.text_diagnostics(root, text, "compiler")
            self.assertEqual(len(results), 1)
            self.assertEqual(results[0]["path"], "src/a.cpp")


if __name__ == "__main__":
    unittest.main()
