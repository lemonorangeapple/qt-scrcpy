#!/usr/bin/env python3
"""A stand-in for the adb server, speaking the real smart-socket protocol, plus a stand-in for the
scrcpy-server running on the phone. Used by tst_scrcpystream.

usage: fake_adb_server.py <adb-port> <h264-file> <state-dir> [<mode>]
mode: ok (default) | no-server (the shell command exits at once, like a version mismatch)
"""
import os, re, socket, struct, sys, threading, time

adb_port = int(sys.argv[1]); sample = open(sys.argv[2], 'rb').read(); state = sys.argv[3]
mode = sys.argv[4] if len(sys.argv) > 4 else 'ok'
os.makedirs(state, exist_ok=True)
forwards = {}          # abstract socket name -> local tcp port
pushed = {}            # remote path -> bytes
lock = threading.Lock()

def log(line):
    with open(os.path.join(state, 'adb.log'), 'a') as f: f.write(line + '\n')

def recv_exact(c, n):
    b = b''
    while len(b) < n:
        d = c.recv(n - len(b))
        if not d: raise EOFError
        b += d
    return b

def read_request(c):
    n = int(recv_exact(c, 4), 16)
    return recv_exact(c, n).decode()

def okay(c): c.sendall(b'OKAY')
def fail(c, msg): c.sendall(b'FAIL' + b'%04x' % len(msg) + msg.encode())

def nal_units():
    starts = [m.start() for m in re.finditer(b'\x00\x00\x01', sample)]
    starts = [s - 1 if s > 0 and sample[s-1] == 0 else s for s in starts]
    return [sample[a:(starts[i+1] if i+1 < len(starts) else len(sample))] for i, a in enumerate(starts)]

def serve_scrcpy(port, cmdline):
    try:
        _serve_scrcpy(port, cmdline)
    except OSError:
        pass            # the client went away mid-stream: that is what stop() does

def _serve_scrcpy(port, cmdline):
    """Plays scrcpy-server: listen on the forwarded port, dummy byte, device name, codec meta, packets."""
    srv = socket.socket(); srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(('127.0.0.1', port)); srv.listen(1)
    time.sleep(0.4)  # the JVM needs a moment: the client must keep retrying
    conn, _ = srv.accept()
    conn.sendall(b'\x00')
    conn.sendall(b'Fake Phone'.ljust(64, b'\x00'))
    conn.sendall(struct.pack('>III', 0x68323634, 160, 96))
    cfg = b''; ts = 0
    for n in nal_units():
        t = n[n.index(b'\x01') + 1] & 0x1f
        if t in (7, 8): cfg += n; continue
        if cfg:
            conn.sendall(struct.pack('>QI', 1 << 63, len(cfg)) + cfg); cfg = b''
        flags = ts | ((1 << 62) if t == 5 else 0)
        conn.sendall(struct.pack('>QI', flags, len(n)) + n); ts += 100000
        time.sleep(0.02)
    time.sleep(0.5)
    conn.close(); srv.close()

def handle_sync(c, serial):
    path = None
    data = b''
    while True:
        hdr = recv_exact(c, 8); cmd = hdr[:4]; n = struct.unpack('<I', hdr[4:])[0]
        if cmd == b'SEND':
            path = recv_exact(c, n).decode().rsplit(',', 1)[0]
        elif cmd == b'DATA':
            data += recv_exact(c, n)
        elif cmd == b'DONE':
            with lock: pushed[path] = data
            log(f'push {path} {len(data)} bytes')
            c.sendall(b'OKAY' + struct.pack('<I', 0))
        elif cmd == b'QUIT':
            return

def handle(c):
    try:
        serial = None
        while True:
            req = read_request(c)
            log('req ' + req)
            if req == 'host:version':
                okay(c); c.sendall(b'0004' + b'0029'); return   # length-prefixed answer, as the real server does
            if req.startswith('host-serial:') and ':forward:' in req:
                m = re.match(r'host-serial:([^:]+):forward:tcp:(\d+);localabstract:(\S+)', req)
                if not m: fail(c, 'bad forward'); return
                with lock: forwards[m.group(3)] = int(m.group(2))
                okay(c); okay(c); return
            if req.startswith('host-serial:') and ':killforward:' in req:
                okay(c); return
            if req.startswith('host:transport:'):
                serial = req.split(':', 2)[2]
                if serial == 'MISSING': fail(c, "device 'MISSING' not found"); return
                okay(c); continue
            if req == 'sync:':
                okay(c); handle_sync(c, serial); return
            if req.startswith('shell:'):
                cmd = req[6:]
                m = re.search(r'scid=([0-9a-f]{8})', cmd)
                okay(c)
                ok_version = ' 3.3.3 ' in (' ' + cmd + ' ')   # the client passes its version as the first argument
                if mode == 'no-server' or not ok_version:
                    c.sendall(b'[server] ERROR: The server version (3.3.3) does not match the client\n'); return
                with lock: path = '/data/local/tmp/scrcpy-server.jar'; have = path in pushed
                if not have:
                    c.sendall(b'Error: Unable to access jarfile\n'); return
                name = 'scrcpy_' + m.group(1)
                for _ in range(50):
                    with lock: port = forwards.get(name)
                    if port: break
                    time.sleep(0.05)
                else:
                    c.sendall(b'[server] ERROR: no forward\n'); return
                log('server-cmd ' + cmd)
                t = threading.Thread(target=serve_scrcpy, args=(port, cmd), daemon=True); t.start()
                try:
                    while c.recv(1): pass          # stays open while the client keeps the shell
                except OSError: pass
                return
            fail(c, 'unknown request ' + req); return
    except (EOFError, OSError):
        pass
    finally:
        try: c.close()
        except OSError: pass

srv = socket.socket(); srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(('127.0.0.1', adb_port)); srv.listen(8)
open(os.path.join(state, 'ready'), 'w').write('1')
while True:
    c, _ = srv.accept()
    threading.Thread(target=handle, args=(c,), daemon=True).start()
