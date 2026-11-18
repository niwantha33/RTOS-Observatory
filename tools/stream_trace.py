#!/usr/bin/env python3
"""Synthetic local sender for the optional Asio capture CLI; not an RTOS adapter."""
import argparse
import pathlib
import socket
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--input',type=pathlib.Path,required=True)
p.add_argument('--port',type=int,default=12345)
a=p.parse_args()
if a.input.stat().st_size>64*1024*1024: p.error('demo input exceeds 64 MiB')
data=a.input.read_bytes()
if not data.endswith(b'\n'): p.error('trace must end with a newline')
with socket.socket() as server:
    server.bind(('127.0.0.1',a.port));server.listen(1)
    print(f'Waiting for one capture client on 127.0.0.1:{a.port}',flush=True)
    connection,_=server.accept()
    with connection: connection.sendall(data)
