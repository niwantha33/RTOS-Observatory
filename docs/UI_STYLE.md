<!-- SPDX-License-Identifier: Apache-2.0 -->
# Desktop typography and language guide

**Scope:** RTOS Observatory desktop, v0.2.1. Use ordinary readable sentence
case, not technical all-caps banners. Keep units where measurements appear.
Do not simplify technical warnings in ways that overstate certainty.

## Presentation

- Body font: 17 px, system Inter Regular preferred (not bundled).
- Fallback: Segoe UI on Windows; Noto/DejaVu/Liberation on Linux; larger
  ImGui built-in font otherwise.
- Theme: graphite/navy, neutral light text, strong selected states.
- Headings: sentence case and whitespace. Avoid monospaced or shrill branding.
- Information columns scale across the available content width.
- Preferences are process-local; no personal trace paths are persisted.

## Terminology

| UI label | Meaning |
| --- | --- |
| Open files | Load normalized trace or checked saved-session directory |
| Execution timeline | Recorded intervals, not invented scheduling states |
| CPU and waits | Per-core CPU evidence and completed resource waits |
| Trace integrity | Capture provenance, lost events and completeness |
| Observed findings | Patterns, not confirmed causes |
| Export investigation report | Safe non-overwriting JSON export |

## Acceptance

Build Windows/Ubuntu Debug and Release, including Windows clang-cl,
run all CTests, and keep the Linux Xvfb real-GL hidden UI smoke test.
Confirm font fallback and readable spacing manually on a real monitor
after deployment; CI cannot certify visual readability or multi-monitor DPI.
