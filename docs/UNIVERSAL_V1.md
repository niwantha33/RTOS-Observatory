# Universal normalized trace contract v1

The machine-readable serialization descriptor is `schemas/universal-v1.json`. This is a TSV protocol descriptor, not a claim that the CLI accepts JSON events. The legacy five-column protocol remains `legacy-tsv`.

First line (literal tabs, no comments before it):

```
RTOS-OBSERVATORY<TAB>1<TAB>firmware-build-id<TAB>rtos-version-id<TAB>clock-id<TAB>1000000
```

Metadata tokens contain 1–128 printable ASCII non-whitespace bytes. The firmware ID should identify the ELF/build; use a digest as the label when available. No file is opened using these labels. One clock and reset epoch per capture. Frequency must be 1,000,000 because row timestamps are already normalized microseconds.

Each subsequent event has six fields:

```
sequence<TAB>timestamp_us<TAB>event<TAB>task<TAB>object<TAB>value
```

Sequence is unsigned 64-bit, strictly increasing; the initial value is arbitrary. Duplicate/out-of-order sequence is rejected. A gap inserts a normalized `DROP` at the following event timestamp, invalidating analysis continuity. A reset requires a new capture. Timestamp is 0..INT64_MAX and nondecreasing; equal-time order is input order. All other fields and event semantics follow `EVENT_SCHEMA.md`. Identifiers contain 1–64 safe printable ASCII bytes; `-` means absent. Comments begin with `#`; blank lines are ignored. CRLF and LF are accepted. Explicit DROP and sequence gaps are reported separately and may describe overlapping loss; do not add them as an exact total.

| Versioned family | Representation |
| --- | --- |
| Task v1 | CREATE, PRIORITY, PERIOD, SWITCH_IN/OUT, TASK_RELEASE/BEGIN/FINISH |
| ISR v1 | ISR_ENTER/EXIT with core ID, nested intervals; identity unavailable |
| Mutex v1 | MUTEX_WAIT/ACQUIRE/UNLOCK with task/resource IDs |
| Semaphore v1 | WAIT_BEGIN/END with `sem:` resource prefix; paired blocked duration |
| Queue v1 | WAIT_BEGIN/END with `queue:` resource prefix; paired blocked duration |
| Clock v1 | Header clock ID + normalized 1 MHz clock, monotonic timestamp |
| Evidence v1 | SESSION, source row number, sequence, DROP, first/last sequence, missing/declared loss counters |

Unknown schema major, event type or decoder ID fails closed. Extensions with new semantics require a new protocol version or decoder, not silent coercion. Priority is normalized 0–255 with larger values higher urgency. No missing values are inferred as observations.
