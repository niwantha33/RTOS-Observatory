# RTOS Observatory: QG-01–QG-06

The checked-in policy and workflows are **public software quality controls only**.
The private ideas/NDA checklist and work-package ZIP are **not published**.

| Package | Implemented check | Qualification |
|---|---|---|
| QG-01 | Native C++20 Clang build and CTest; compiler/version/build-log capture | GitHub CI validation pending |
| QG-02 | Check-only clang-format, clang-tidy, Cppcheck for project-owned code | GitHub CI validation pending |
| QG-03 | Compare diagnostics against PR base commit; added errors fail, warnings advise | Controlled unit tests included |
| QG-04 | Separate host ASan+UBSan CTest job | GitHub CI validation pending |
| QG-05 | Gitleaks v3 and GitHub dependency-review v5 on PR | Branch protection is an admin follow-up |
| QG-06 | Linux CLI SHA-256, Sigstore/GitHub OIDC attestation, verified GitHub workflow/source identity | Requires an approved future `v*` tag |

## Quality gating

PR base commit = approved baseline. Every PR runs both head and base using the
**same** quality configuration and pinned runner environment. Findings are
normalized to the project-owned path, tool, severity, rule and message. Line/column
are excluded from the identity. A repeated finding beyond its baseline count is new.

* New **errors** (including formatting errors) fail the quality delta.
* New **warnings** are reported but advisory until triaged.
* Third-party submodules and generated code are never included.
* A formatter never changes sources; no bulk reformat is done.
* Analyzer launch errors, missing compilation databases or invalid output fail closed.
* Analysis evidence is retained as GitHub Actions artifacts for 14 days.

Local gates (Linux tools installed, dependencies initialized):

```bash
python -m unittest discover -s tests -p test_quality_gate.py -v
cmake -S . -B quality-build -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DOBSERVATORY_BUILD_LIVE=ON -DOBSERVATORY_BUILD_DESKTOP=ON
cmake --build quality-build
ctest --test-dir quality-build --output-on-failure
python tools/quality_gate.py collect --source . --build quality-build \
  --output diagnostics/candidate.json
```

The PR workflow builds and analyzes both the approved base and candidate; local
collection alone does **not** certify that no new errors were introduced.

## Required GitHub rules (manual repository-admin action after checks turn green)

Require review, block direct pushes to `main`, and require the status checks
`Quality gate / baseline delta`, `Sanitizers / ASan + UBSan`,
`Security / secret scanning`, `Security / dependency review`, plus the existing
native, desktop, clang-cl and autopilot checks. Do not claim branch protection is
active until GitHub settings confirm it.

The quality and security workflows use `pull_request`, not
`pull_request_target`, and never expose elevated write tokens to PR code.

## Official provenance

An intentional `v*` tag triggers a Linux CLI build and CTest, packaging,
SHA-256 checksums, `actions/attest@v4` OIDC signing, and a `gh attestation verify`
check enforcing the expected repository, signer workflow, tag and source commit.
Only verified packages upload as run artifacts. No release is published automatically.

To independently verify the downloaded archive, retain the exact archive bytes
and run:

```bash
sha256sum --check SHA256SUMS
gh attestation verify rtos-observatory-vX.Y.Z-linux-x64.tar.gz \
  --repo niwantha33/RTOS-Observatory \
  --signer-workflow niwantha33/RTOS-Observatory/.github/workflows/release-integrity.yml \
  --source-ref refs/tags/vX.Y.Z
```

No hardware test, board, device firmware, real-time capture or timing measurement
is asserted as qualified by these host-only quality gates.
