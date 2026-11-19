<!-- SPDX-License-Identifier: Apache-2.0 -->
# Step-by-step implementation: desktop application shell

This package improves the offline Dear ImGui desktop on top of the merged W05/W06
baseline. It does **not** imply live tracing or debugger integration.

| Step | Implementation | Verification |
| --- | --- | --- |
| 1 | Read current desktop main/workspace and preserve existing analysis model | CMake & CTest baseline |
| 2 | Generate project-version, Git HEAD and UTC configure metadata in CMake | `--version` metadata CTest |
| 3 | Add working File, Settings, Window, Tools and Help menus | Hidden real-GL UI smoke and manual navigation |
| 4 | Add Open/Export/About dialogs and safe Exit handling | Existing session integrity and no-overwrite rules unchanged |
| 5 | Add footer, versioned title and clearer wording | Windows/Linux Debug + Release desktop tests |
| 6 | Review CI, merge PR into main and update tracker | Links to merged commit and validated CI |

## Explicit boundaries

- File commands accept typed paths. Native OS chooser is planned separately.
- The app remains offline only; no target-side command or inferred RTOS state is added.
- Git commit is the CMake-configured checkout, not a firmware identity or attestation.
- Build time shown is configure UTC (honours SOURCE_DATE_EPOCH), not linker or package timestamp.
- Settings and recent files stay process-local; no evidence identity is persisted.
- A passing headless model test alone does not qualify multi-DPI rendering or real MCU traces.
