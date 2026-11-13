"""Local fake GitHub release server used by tests/test_firmware_store.swift."""
import hashlib, json, os, struct, sys
from http.server import BaseHTTPRequestHandler, HTTPServer

def image(size, seed, chip=13):
    body = bytearray(os.urandom(size)); body[0] = 0xE9; struct.pack_into('<H', body, 12, chip); body[1] = seed
    return bytes(body)

def good_set():
    files = {'bootloader.bin': image(20000, 1), 'partition-table.bin': b'\xaa\x50' + os.urandom(3070), 'ronin_c6.bin': image(300000, 2)}
    manifest = {n: hashlib.sha256(d).hexdigest() for n, d in files.items()}
    return files, manifest

def build(variant, port):
    files, manifest = good_set()
    tag = {'old': 'v1.0.0'}.get(variant, 'v9.9.9')
    assets = {'firmware-manifest.json': json.dumps(manifest).encode()}
    for n, d in files.items(): assets['firmware-' + n] = d
    listing = []
    served = dict(assets)
    for name, data in assets.items():
        digest = 'sha256:' + hashlib.sha256(data).hexdigest()
        if variant == 'missing' and name == 'firmware-ronin_c6.bin': continue
        listing.append({'name': name, 'size': len(data), 'digest': digest,
                        'browser_download_url': f'http://127.0.0.1:{port}/{variant}/{name}'})
    if variant == 'tampered':      # served bytes differ from the digest GitHub advertises
        served['firmware-ronin_c6.bin'] = served['firmware-ronin_c6.bin'][:-1] + b'\x00'
    if variant == 'manifest':      # no digest from the API; the manifest must catch it
        served['firmware-ronin_c6.bin'] = served['firmware-ronin_c6.bin'][:-1] + b'\x00'
        for a in listing: a.pop('digest')
    if variant == 'wrongchip':     # valid hashes but built for another chip
        files['ronin_c6.bin'] = image(300000, 2, chip=5)
        manifest = {n: hashlib.sha256(d).hexdigest() for n, d in files.items()}
        served['firmware-ronin_c6.bin'] = files['ronin_c6.bin']; served['firmware-manifest.json'] = json.dumps(manifest).encode()
        for a in listing: a.pop('digest')
    release = json.dumps({'tag_name': tag, 'assets': listing}).encode()
    return release, served

class Handler(BaseHTTPRequestHandler):
    cache = {}
    def log_message(self, *a): pass
    def do_GET(self):
        parts = self.path.strip('/').split('/')
        variant, name = parts[0], '/'.join(parts[1:])
        if variant not in self.cache: self.cache[variant] = build(variant, self.server.server_port)
        release, served = self.cache[variant]
        body = release if name == 'release.json' else served.get(name)
        if body is None: self.send_response(404); self.end_headers(); return
        self.send_response(200); self.send_header('Content-Length', str(len(body))); self.end_headers(); self.wfile.write(body)

server = HTTPServer(('127.0.0.1', 0), Handler)
print(server.server_port, flush=True)
server.serve_forever()
