"""Real loopback TCP, deliberately fragmented input, bounded failures and replay."""
import pathlib
import socket
import subprocess
import sys
import tempfile
import threading
import time
capture, analyze, fixture = sys.argv[1:]
# Git may check the fixture out as CRLF on Windows. Normalize before
# constructing explicit LF/CRLF wire cases; avoid generating CRCRLF.
trace=pathlib.Path(fixture).read_bytes().replace(b'\r\n',b'\n')
def check(data, expected=0, extra=(), delay=0):
    with tempfile.TemporaryDirectory() as tmp:
        root=pathlib.Path(tmp);session=root/'session'
        with socket.socket() as listener:
            listener.bind(('127.0.0.1',0));listener.listen();listener.settimeout(5)
            port=listener.getsockname()[1]
            errors=[]
            def serve():
                try:
                    conn,_=listener.accept()
                    with conn:
                        if delay: time.sleep(delay)
                        for start in range(0,len(data),3): conn.sendall(data[start:start+3])
                except (BrokenPipeError,ConnectionResetError,ConnectionAbortedError): pass
                except Exception as e: errors.append(e)
            worker=threading.Thread(target=serve);worker.start()
            result=subprocess.run([capture,'--port',str(port),'--store',str(session),*extra],capture_output=True,text=True,timeout=8)
            worker.join(timeout=6)
            assert not worker.is_alive() and not errors,errors
        assert result.returncode==expected,(result.returncode,result.stdout,result.stderr)
        if expected in (0,3):
            assert (session/'raw.trace').read_bytes()==data
            report=root/'report.json'
            replay=subprocess.run([analyze,'--replay',str(session),'--output',str(report)],capture_output=True,timeout=5)
            assert replay.returncode==expected,replay.stderr
        else: assert not session.exists(),result.stderr
check(trace)
check(trace.replace(b'\n',b'\r\n'))
check(trace.replace(b'3\t20',b'5\t20'),expected=3)
check(trace[:-1],expected=1)
check(b'garbage\n',expected=1)
check(trace,expected=1,extra=('--max-bytes','32'))
check(b'#'+b'x'*5000+b'\n',expected=1)
check(trace,expected=1,extra=('--idle-ms','50'),delay=0.15)
check(trace,expected=1,extra=('--duration-ms','50','--idle-ms','1000'),delay=0.15)
print('TCP capture tests passed')
