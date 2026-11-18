"""Standalone CLI exit gate, exercised on Linux and Windows."""
import pathlib
import subprocess
import sys
import tempfile
cli, fixture = sys.argv[1:]
def run(*args, expected=0):
    p = subprocess.run([cli, *map(str,args)], capture_output=True, text=True)
    if p.returncode != expected:
        raise AssertionError((p.returncode,p.stdout,p.stderr))
with tempfile.TemporaryDirectory() as tmp:
    root=pathlib.Path(tmp)
    session=root/'session'
    a,b=root/'a.json',root/'b.json'
    run('--decoder','universal-v1','--input',fixture,'--store',session,'--output',a)
    run('--replay',session,'--output',b)
    assert a.read_bytes()==b.read_bytes()
    run('--replay',session,'--output',session/'raw.trace',expected=1)
    run('--decoder','universal-v1','--input',fixture,'--store',session,'--output',a,expected=1)
    (session/'raw.trace').write_bytes((session/'raw.trace').read_bytes().replace(b'worker',b'Worker'))
    run('--replay',session,'--output',b,expected=1)
