# SPDX-License-Identifier: Apache-2.0
"""Check that the desktop reports real, parseable embedded build metadata."""
import re
import subprocess
import sys

binary, expected_version = sys.argv[1:3]
result = subprocess.run([binary, "--version"], check=True,
                        capture_output=True, text=True, timeout=15)
output = result.stdout
assert f"RTOS Observatory {expected_version}\n" in output, output
assert re.search(r"^Commit: (?:[0-9a-fA-F]{7,40}|unknown)$", output, re.M), output
assert re.search(
    r"^Build configured \(UTC\): \d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2} UTC$",
    output, re.M), output
print("Desktop build version/commit/UTC metadata verified")
