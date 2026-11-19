<!-- SPDX-License-Identifier: Apache-2.0 -->
# W14 — Passive Eclipse CDT/DSF-GDB session bridge (software-only)

**Delivery state:** Source integration proposal + native metadata parser/GUI and deterministic CTests. Eclipse PDE/Tycho installation, physical GDB/XDS110/OpenOCD, and actual target behavior are **NOT qualified or claimed**. No Observatory code opens a competing debugger connection.

## Design / owner model

```text
Eclipse CDT / DSF-GDB = sole existing debugger owner
          |
    selected GdbLaunch.getSession()
          |
    DSF IRunControl resume / suspend event listener
    Eclipse ILaunchesListener2 termination/removal listener
          |
    metadata-only, 2s publisher heartbeat
    atomic replace (local file, opt-in, user-selected directory)
          |
   RTOS Observatory passive status reader (manual Refresh)
          |
   stale/epoch/sequence/loss checks + GUI
```

The Eclipse handler uses DSF's **own** executor to register listeners; file publication occurs on a separate single-threaded Java executor. It never calls GDB/MI services, reads target memory, sets breakpoints, halts, resumes, resets, flashes, or opens a socket. This package **does not** make a debug remote-control/API channel. See [Eclipse DSF programming model](https://help.eclipse.org/latest/topic/org.eclipse.cdt.doc.isv/guide/dsf/intro/dsf_programming_intro.html) and [Eclipse launch events](https://help.eclipse.org/latest/topic/org.eclipse.platform.doc.isv/reference/api/org/eclipse/debug/core/ILaunchesListener2.html) for the public listener model.

## W14 publication v1

Exactly one UTF-8/ASCII **tab-separated line ending with LF**, atomically published by Eclipse into `rtos-observatory-dsf-status.tsv`. Maximum 4096 bytes.

```text
RTOS-OBSERVATORY-DSF<TAB>1<TAB>nonce<TAB>launch-label<TAB>dsf-session-label<TAB>epoch<TAB>sequence<TAB>writer-unix-ms<TAB>state<TAB>metadata-only<LF>
```

- Allowed states: `ATTACHED`, `RUNNING`, `SUSPENDED`, `UNKNOWN`, `DISCONNECTED`, `TERMINATED`. State is **last reported DSF event**, never independently verified present target state.
- Launch/session IDs and nonce are opaque self-reported labels; **not authentication secrets**, host/user/process identity, verified target/ELF provenance or a cryptographic session pairing.
- The Java writer provides a 2-second **metadata-only heartbeat**. A recent heartbeat does **not** imply the MCU is running, stopped, responsive, synchronized or even connected correctly.
- The native reader refuses malformed/partial/duplicate appended records, symlinked file paths, unsupported capability tokens, missing fields, bad numbers, invalid control characters, backward timestamps/sequence and same-epoch resurrection after termination. The reader accepts an initial late attachment **without pretending the initial transitions were observed**, accepts an unchanged manual refresh, flags missing sequence values, and requires an explicit reset before switching identity/epoch.
- **Freshness** is based only on self-reported Unix host time and a local 5000 ms default window (no clock synchronization proof). Missing publisher updates are **unknown**, not "disconnected" or "suspended".
- Only a local file is read. No outbound network usage or target data is involved. No automatic file polling; user must click Refresh to see a new publication.

## Eclipse source integration (not installed)

The source-only Eclipse handler lives at `bridge/eclipse/DsfLifecycleBridgeHandler.java`. Integrate it into the existing Zephyr Eclipse Workbench UI plugin's package `io.github.niwantha33.zew.ui`; merge the new command+handler+menu nodes from `bridge/eclipse/plugin-extensions.xml` into its `plugin.xml`. Add PDE bundle dependencies for `org.eclipse.cdt.dsf`, `org.eclipse.cdt.dsf.gdb`, `org.eclipse.debug.core`, `org.eclipse.debug.ui`, `org.eclipse.ui` and JFace/SWT as needed. Qualify these dependencies and run the actual Tycho/UI plugin tests in **ZEW**; this repository cannot attest they build.

The user must create/select a **private local** directory and set `RTOS_OBSERVATORY_BRIDGE_DIR` before starting Eclipse, then select an existing GdbLaunch in the Debug view and choose **Toggle RTOS Observatory (Passive DSF Lifecycle)**. A second click stops publication. The handler automatically stops when the selected launch terminates/is removed and attempts a terminal status publication. The reader does not automatically load data from other directories or sessions.

Open a record in the native workbench with `--bridge-status PATH` or via **Debugger lifecycle** panel, choose **Refresh local status**. **Forget previous session / epoch** clears only local status acceptance history (not the debugger). The supplied `examples/dsf-status.tsv` is synthetic and deliberately stale; it is not live hardware evidence.

## Quality gates

- `observatory_dsf_bridge_lifecycle`: state model, monotonic sequence, identity, late attach, stale timestamps, session termination, malformed input and capability denial; no Java/Eclipse dependency.
- `observatory_desktop_bridge_smoke`: native model headless acceptance of a synthetic but stale metadata publication.
- `observatory_imgui_bridge_ui_smoke`: Linux Xvfb/GLFW hidden GUI render for the passive panel.
- Native Windows/Linux Debug+Release and Windows clang-cl Debug+Release, independent review, and **real Eclipse plugin build/integration tests** remain required for W14 completion.
- Physical board/hardware qualification explicitly **deferred to W19**. The W15 memory/register/TCB reader remains separate, requires user consent, operation safety and target snapshot coherence checks.

**No production live debugger or installed Eclipse DSF extension is claimed by W14.**
