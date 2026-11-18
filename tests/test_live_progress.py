"""Prove progress and rejection occur while the sender is still connected."""
import pathlib
import socket
import subprocess
import sys
import tempfile
import threading
capture,fixture=sys.argv[1:]
raw=pathlib.Path(fixture).read_bytes().replace(b'\r\n',b'\n')
lines=raw.splitlines(keepends=True)
with tempfile.TemporaryDirectory() as tmp:
    session=pathlib.Path(tmp)/'session'
    with socket.socket() as server:
        server.bind(('127.0.0.1',0));server.listen();server.settimeout(5)
        process=subprocess.Popen([capture,'--port',str(server.getsockname()[1]),'--store',str(session)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        try:
            conn,_=server.accept()
            with conn:
                seen=threading.Event();output=[]
                def read_output():
                    for line in process.stdout:
                        output.append(line)
                        if 'Live events=2 ' in line and 'finalized=0' in line: seen.set()
                reader=threading.Thread(target=read_output);reader.start()
                conn.sendall(b''.join(lines[:3]))
                assert seen.wait(3),'no progress before source EOF'
                assert process.poll() is None,'capture ended before source EOF'
                conn.sendall(b''.join(lines[3:]))
                conn.shutdown(socket.SHUT_WR)
            process.wait(timeout=5);reader.join(timeout=5)
            assert process.returncode==0,process.stderr.read()
            assert any('finalized=1' in line for line in output),output
            assert (session/'raw.trace').read_bytes()==raw
        finally:
            if process.poll() is None: process.kill();process.wait()
    with socket.socket() as server:
        server.bind(('127.0.0.1',0));server.listen();server.settimeout(5)
        bad_session=pathlib.Path(tmp)/'bad'
        process=subprocess.Popen([capture,'--port',str(server.getsockname()[1]),'--store',str(bad_session)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        try:
            conn,_=server.accept()
            with conn:
                conn.settimeout(3)
                conn.sendall(lines[0]+b'0\t0\tSESSION\t-\ts\t0\n1\t1\tBOGUS\t-\t-\t0\n')
                # Keep the source open. Parser must close before the 5s idle timeout.
                try:
                    assert conn.recv(1)==b''
                except ConnectionResetError:
                    pass  # Reset is also an immediate receiver-side close.
            out,err=process.communicate(timeout=5)
            assert process.returncode==1 and 'unsupported event' in err,(out,err)
            assert not bad_session.exists()
        finally:
            if process.poll() is None: process.kill();process.wait()
print('Live progress and fail-fast tests passed')
