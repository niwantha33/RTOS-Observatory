# Normalised trace event contract v0.1 (synthetic/offline)

The input is **UTF-8 text**, five **tab-separated** fields per row:

```
timestamp_us<TAB>event<TAB>task<TAB>object<TAB>value
```

All timestamps must be monotonic and within the signed 64-bit range. Equal timestamps retain file order. A `#` at the first character denotes a comment line; no quoting or variable columns are allowed. Field names and IDs are 1..64 printable ASCII bytes without whitespace. `-` means no object/task. The numeric value is a signed 64-bit decimal integer.

The first event must be `0 SESSION - <session-id> 0` (with tabs). Resets require **separate captures**. `synthetic-...` means generated illustrative data; it is not a validation receipt. The **data source is not authenticated** by this format.

## Event semantics

| Event | Task | Object | Value | Interpretation |
| --- | --- | --- | --- | --- |
| `SESSION` | `-` | session ID | `0` | Begin one epoch |
| `CREATE` | task ID | `idle` or `-` | normalised priority (0..255) | Task identity definition |
| `PRIORITY` | task ID | `-` | normalised priority (0..255) | Effective priority change |
| `PERIOD` | task ID | `-` | period in µs (positive) | Declared cycle period |
| `SWITCH_IN` / `SWITCH_OUT` | task ID | core ID | `0` | Explicit running interval endpoints |
| `ISR_ENTER` / `ISR_EXIT` | `-` | core ID | `0` | Nested ISR interval (not ISR identity) |
| `MUTEX_ACQUIRE` / `MUTEX_UNLOCK` | task ID | mutex ID | `0` | Proven ownership acquire/release |
| `MUTEX_WAIT` | task ID | mutex ID | `0` | Begin completed wait, paired with acquire |
| `WAIT_BEGIN` / `WAIT_END` | task ID | resource ID | `0` | Generic blocked/wakeup wait |
| `TASK_RELEASE` | task ID | `-` | `0` | Explicit period release marker |
| `TASK_BEGIN` / `TASK_FINISH` | task ID | `-` | `0` | Application-cycle begin/finish markers |
| `DROP` | `-` | `-` | lost count > 0 | Discontinuity; invalidate state |

Use `sem:`, `queue:`, `gate:`, `mailbox:`, `event:`, and `notify:` prefixes in generic resource IDs for **display classification only**. `MUTEX_*` event types identify mutex operations even when the object has no `mutex:` prefix. Generic `WAIT_BEGIN` / `WAIT_END` does **not** prove semaphore count, queue depth, gate admission, mailbox occupancy, event-bit values, notification values or a wakeup cause. Names must not be interpreted as qualified vendor RTOS semantics; see [W10 resource observations](W10_RESOURCES.md).

This schema is **not a claimed raw Zephyr CTF parser**. A source adapter must verify its original format, metadata, clock basis, task ID reuse, buffer-loss information, priority direction and RTOS version before emitting normalised events. This prototype consumes only the normalised format.

## Report output

`rtos-observatory-report/v1` JSON contains `source`, `session`, `complete`, `start_us`, `end_us`, `threads`, `intervals`, `waits`, `cycles`, `findings`, `warnings`.

A `PRIORITY_INVERSION_PATTERN` requires three observed facts: higher-priority waiting task, lower-priority recorded mutex owner at the start of the wait, and medium-priority execution overlapping the period the owner still held the mutex. It is not a proof of full priority inheritance behavior or deadline failure. Events from different cores may need further causal filtering; this prototype is **not yet multi-core-qualified**.

`phase_error_us` is the difference between the nth observed release and `first_observed_release + n * period`, not drift from an external oscillator. `DISPATCH_DELAY` currently refers to explicit application `TASK_BEGIN` markers; it is **not necessarily an RTOS switch-in latency**.
