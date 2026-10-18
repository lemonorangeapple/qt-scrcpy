#!/usr/bin/env python3
"""Runs tst_cinterface against the fake adb server. usage: run_cinterface.py <exe> <fake_adb_server.py> <sample.h264>"""
import os, socket, subprocess, sys, tempfile, time

exe, server, sample = sys.argv[1:4]
with socket.socket() as s:
    s.bind(('127.0.0.1', 0)); port = s.getsockname()[1]
work = tempfile.mkdtemp(prefix='qsc-c-')
jar = os.path.join(work, 'scrcpy-server')
open(jar, 'wb').write(b'PK fake jar')
proc = subprocess.Popen([sys.executable, server, str(port), sample, os.path.join(work, 'state')])
try:
    for _ in range(100):
        if os.path.exists(os.path.join(work, 'state', 'ready')): break
        time.sleep(0.05)
    env = dict(os.environ, ANDROID_ADB_SERVER_PORT=str(port), SCRCPY_SERVER_PATH=jar)
    env.pop('ADB', None); env.pop('HDC', None)
    rc = subprocess.call([exe], env=env, timeout=180)
finally:
    proc.kill()
sys.exit(rc)
