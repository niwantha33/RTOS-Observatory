# SPDX-License-Identifier: Apache-2.0
"""W17 real loopback TCP, deliberate fragments, CRC/sequence gaps and timeouts."""
import pathlib
import socket
import struct
import subprocess
import sys
import threading
import time
import zlib

capture = sys.argv[1]

def frame(kind, epoch, sequence, payload):
    header = struct.pack("<4sBBHII", b"RTOB", 1, kind, len(payload), epoch, sequence)
    checksum = zlib.crc32(header + payload)
    return header + payload + struct.pack("<I", checksum)

def event(epoch, frame_seq, attempt, timestamp=1234):
    payload = struct.pack("<QIIHHIHH", timestamp, 42, 50, 9, 0, attempt, 0, 1)
    assert len(payload) == 28
    return frame(1, epoch, frame_seq, payload)

def stats(epoch, frame_seq):
    payload = struct.pack("<HHIIIII", 0, 0, 2, 2, 0, 0, 0)
    assert len(payload) == 24
    return frame(2, epoch, frame_seq, payload)

def check(data, expected, contains=(), options=(), delay=0):
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
        server.bind(("127.0.0.1", 0))
        server.listen(1)
        server.settimeout(5)
        port = server.getsockname()[1]
        errors = []

        def serve():
            try:
                conn, _ = server.accept()
                with conn:
                    if delay:
                        time.sleep(delay)
                    for index in range(0, len(data), 3):
                        conn.sendall(data[index:index + 3])
            except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
                pass
            except Exception as error:
                errors.append(error)

        worker = threading.Thread(target=serve)
        worker.start()
        result = subprocess.run(
            [capture, "--port", str(port), *options],
            capture_output=True, text=True, timeout=8
        )
        worker.join(timeout=6)
        assert not worker.is_alive() and not errors, errors
        assert result.returncode == expected, (
            result.returncode, result.stdout, result.stderr
        )
        message = result.stdout + result.stderr
        for piece in contains:
            assert piece in message, message

valid = event(7, 1, 1) + stats(7, 2)
check(valid, 0, ("frames=2", "events=1", "stats=1", "evidence=NO_OBSERVED_LOSS"))

bad = bytearray(event(7, 2, 2))
bad[25] ^= 0x55  # wrong CRC; next accepted frame has gap
check(event(7, 1, 1) + bytes(bad) + event(7, 3, 3),
      3, ("crc_errors=1", "missing_frames=1", "evidence=INCOMPLETE"))

check(event(7, 1, 1) + event(7, 1, 1),
      3, ("replayed=1", "evidence=INCOMPLETE"))
check(event(7, 1, 1) + event(8, 1, 1),
      3, ("epoch_changes=1", "evidence=INCOMPLETE"))
check(event(7, 1, 1)[:-1], 1, ("truncated record",))
check(b"not-a-frame", 1, ("no valid frames",))
check(valid, 1, ("byte budget",), ("--max-bytes", "48"))
check(valid, 1, ("frame budget",), ("--max-frames", "1"))
check(valid, 1, ("idle timeout",), ("--idle-ms", "60"), 0.2)
check(valid, 1, ("duration exceeded",),
      ("--duration-ms", "60", "--idle-ms", "1000"), 0.2)
print("W17 framed TCP loopback/CRC/fragment/timeouts passed")
