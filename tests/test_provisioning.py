"""Exercise native serial framing over a pseudo-terminal, without hardware."""
import errno
import json
import os
import pty
import subprocess
import threading
import time


def exchange(mode):
    master, slave = pty.openpty()
    path = os.ttyname(slave)
    os.close(slave)
    errors = []

    def emulator():
        data = bytearray()
        deadline = time.monotonic() + 8
        try:
            while time.monotonic() < deadline:
                try:
                    chunk = os.read(master, 1024)
                except OSError as error:
                    if error.errno == errno.EIO:
                        time.sleep(0.01)
                        continue
                    raise
                data.extend(chunk)
                if b'\n' not in data:
                    continue
                line, _, rest = data.partition(b'\n')
                data = bytearray(rest)
                request = json.loads(line.decode().split('RONIN ', 1)[1])
                if request['op'] == 'wifi.set':
                    assert mode == 'configure'
                    assert request['ssid'] == 'Réseau \"test\"'
                    assert request['password'] == 'test-only-password'
                else:
                    assert request['op'] == 'wifi.status'
                if mode == 'timeout':
                    return
                reply = {'id': request['id'], 'ok': mode != 'refused', 'error': 'unsupported_operation',
                         'ssid': 'Réseau test', 'connected': True, 'ip': '192.0.2.12'}
                stale = dict(reply, id=request['id'] + 1)
                os.write(master, b'boot log\nRONIN invalid-json\n' + b'RONIN ' + json.dumps(stale).encode() + b'\n')
                wire = b'RONIN ' + json.dumps(reply).encode() + b'\n'
                os.write(master, wire[:11])
                time.sleep(0.02)
                os.write(master, wire[11:])
                if mode == 'configure' and request['op'] == 'wifi.status':
                    continue
                return
        except Exception as error:
            errors.append(error)

    thread = threading.Thread(target=emulator)
    thread.start()
    arguments = ['/tmp/ronin-provision-test', path] + (['configure'] if mode == 'configure' else [])
    result = subprocess.run(arguments, capture_output=True, text=True, timeout=10)
    thread.join(timeout=9)
    os.close(master)
    assert not errors, errors
    if mode in ('ok', 'configure'):
        assert result.returncode == 0, result.stderr
        assert 'SSID=Réseau test;CONNECTED=true;IP=192.0.2.12' in result.stdout
    else:
        assert result.returncode != 0


for scenario in ('ok', 'configure', 'refused', 'timeout'):
    exchange(scenario)
print('USB provisioning: fragmented replies, stale IDs, malformed lines, rejection and timeout passed')
