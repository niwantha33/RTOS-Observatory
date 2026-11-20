#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generate exclusively synthetic CTF for native W20 GUI smoke tests."""
from pathlib import Path
import struct
import shutil
import sys

out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
metadata = """/* CTF 1.8 */
typealias integer { size = 16; align = 8; signed = false; } := uint16_t;
typealias integer { size = 32; align = 8; signed = false; } := uint32_t;
typealias integer { size = 32; align = 8; signed = true; } := int32_t;
struct event_header { uint32_t timestamp; uint16_t id; };
trace { major = 1; minor = 8; byte_order = le; };
stream { event.header := struct event_header; };
event {
 name = thread_switched_in;
 id = 0x10;
 fields := struct { uint32_t thread_id; };
};
event {
 name = thread_switched_out;
 id = 0x11;
 fields := struct { uint32_t thread_id; };
};
event {
 name = mutex_lock_blocking;
 id = 0x2a;
 fields := struct { uint32_t id; };
};
event {
 name = mutex_lock_exit;
 id = 0x2b;
 fields := struct { uint32_t id; int32_t ret; };
};
event {
 name = mutex_unlock_enter;
 id = 0x2c;
 fields := struct { uint32_t id; };
};
event {
 name = semaphore_take_blocking;
 id = 0x25;
 fields := struct { uint32_t id; };
};
event {
 name = semaphore_give_enter;
 id = 0x22;
 fields := struct { uint32_t id; };
};
event {
 name = semaphore_take_exit;
 id = 0x26;
 fields := struct { uint32_t id; int32_t ret; };
};
"""
(out / "metadata").write_text(metadata, encoding="ascii")
events = [
    (100, 0x10, 41),      # one thread
    (110, 0x2b, 99, 0),   # mutex acquired (observed callback)
    (120, 0x11, 41),
    (130, 0x10, 42),      # waiter
    (140, 0x2a, 99),      # waiting on mutex
    (150, 0x11, 42),
    (160, 0x10, 41),
    (165, 0x2c, 99),      # owner entered unlock
    (170, 0x11, 41),
    (175, 0x10, 42),
    (180, 0x2b, 99, 0),   # waiter acquired
    (190, 0x25, 55),
    (195, 0x11, 42),
    (205, 0x10, 41),
    (210, 0x22, 55),
    (215, 0x11, 41),
    (220, 0x10, 42),
    (240, 0x26, 55, 0),
]
buf = bytearray()
for tick, kind, *payload in events:
    buf += struct.pack("<IH", tick, kind)
    fmt = "<Ii" if len(payload) == 2 else "<I"
    buf += struct.pack(fmt, *payload)
(out / "channel0_0").write_bytes(buf)
# Exercise folder browsing/auto-detection with an ordinary synthetic subdirectory.
nested = out / "wrapped" / "capture"
nested.mkdir(parents=True, exist_ok=True)
shutil.copyfile(out / "metadata", nested / "metadata")
shutil.copyfile(out / "channel0_0", nested / "channel0_0")
print("W20 synthetic CTF created (contains no user/private trace)")
