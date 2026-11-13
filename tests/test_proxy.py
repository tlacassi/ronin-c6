import base64
import hashlib
import http.client
import http.server
import socket
import threading
import unittest
import urllib.parse
from tools.ronin_proxy import make_handler


class Upstream(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200)
        self.end_headers()
        self.wfile.write(b'bridge')

    def do_POST(self):
        body = self.rfile.read(int(self.headers['Content-Length']))
        self.send_response(200)
        self.end_headers()
        self.wfile.write(self.headers.get('X-Ronin-Session', '').encode() + b':' + body)

    def log_message(self, *args):
        pass


GUID = '258EAFA5-E914-47DA-95CA-C5AB0DC85B11'


class FakeEsp:
    """Raw TCP stand-in for the ESP WebSocket: checks the upstream handshake, then echoes bytes."""
    def __init__(self, accept=True):
        self.accept = accept
        self.requests = []
        self.closed = threading.Event()
        self.server = socket.socket()
        self.server.bind(('127.0.0.1', 0))
        self.server.listen(5)
        self.port = self.server.getsockname()[1]
        threading.Thread(target=self.serve, daemon=True).start()

    def serve(self):
        while True:
            try:
                conn, _ = self.server.accept()
            except OSError:
                return
            threading.Thread(target=self.handle, args=(conn,), daemon=True).start()

    def handle(self, conn):
        data = b''
        while b'\r\n\r\n' not in data:
            chunk = conn.recv(1024)
            if not chunk:
                return
            data += chunk
        self.requests.append(data.decode())
        key = [l.split(': ', 1)[1] for l in data.decode().split('\r\n') if l.lower().startswith('sec-websocket-key')][0]
        if not self.accept:
            conn.sendall(b'HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n')
            conn.close()
            return
        digest = base64.b64encode(hashlib.sha1((key + GUID).encode()).digest()).decode()
        conn.sendall(f'HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: {digest}\r\n\r\n'.encode() + b'hello')
        while True:
            chunk = conn.recv(1024)
            if not chunk:
                self.closed.set()
                return
            conn.sendall(chunk)


class WebSocketProxyTest(unittest.TestCase):
    KEY = base64.b64encode(b'0123456789abcdef').decode()

    def start(self, accept=True):
        self.esp = FakeEsp(accept)
        target = urllib.parse.urlsplit(f'http://127.0.0.1:{self.esp.port}')
        self.proxy = http.server.ThreadingHTTPServer(('127.0.0.1', 0), make_handler(target))
        threading.Thread(target=self.proxy.serve_forever, daemon=True).start()
        self.addCleanup(lambda: (self.proxy.shutdown(), self.proxy.server_close(), self.esp.server.close()))
        self.port = self.proxy.server_port

    def handshake(self, host=None, origin=None, extra=''):
        sock = socket.create_connection(('127.0.0.1', self.port), timeout=3)
        self.addCleanup(sock.close)
        host = host or f'127.0.0.1:{self.port}'
        origin_line = f'Origin: {origin}\r\n' if origin is not None else ''
        sock.sendall((f'GET /ws HTTP/1.1\r\nHost: {host}\r\n{origin_line}Upgrade: websocket\r\nConnection: Upgrade\r\n'
                      f'Sec-WebSocket-Key: {self.KEY}\r\nSec-WebSocket-Version: 13\r\n{extra}\r\n').encode())
        data = b''
        while b'\r\n\r\n' not in data:
            chunk = sock.recv(1024)
            if not chunk:
                break
            data += chunk
        return sock, data

    def test_relays_and_rewrites_origin(self):
        self.start()
        sock, data = self.handshake(origin=f'http://127.0.0.1:{self.proxy_port()}')
        self.assertTrue(data.startswith(b'HTTP/1.1 101'))
        expected = base64.b64encode(hashlib.sha1((self.KEY + GUID).encode()).digest())
        self.assertIn(expected, data)
        upstream_request = self.esp.requests[0]
        self.assertIn(f'Host: 127.0.0.1:{self.esp.port}', upstream_request)
        self.assertIn(f'Origin: http://127.0.0.1:{self.esp.port}', upstream_request)
        self.assertIn('GET /ws HTTP/1.1', upstream_request)
        rest = data.split(b'\r\n\r\n', 1)[1]
        while len(rest) < 5:
            rest += sock.recv(16)
        self.assertEqual(rest, b'hello')
        sock.sendall(b'abc')
        self.assertEqual(sock.recv(16), b'abc')
        sock.close()
        self.assertTrue(self.esp.closed.wait(3), 'closing the browser side must close the ESP socket')

    def proxy_port(self):
        return self.port

    def test_rejects_foreign_origin_missing_origin_and_foreign_host(self):
        self.start()
        for kwargs in ({'origin': 'http://evil.example'}, {}, {'origin': f'http://localhost:{self.port + 1}'},
                       {'host': 'ronin-c6.local', 'origin': 'http://ronin-c6.local'}):
            with self.subTest(kwargs=kwargs):
                _, data = self.handshake(**kwargs)
                self.assertTrue(data.startswith(b'HTTP/1.0 403'), data[:40])
        self.assertEqual(self.esp.requests, [])

    def test_rejects_malformed_upgrade(self):
        self.start()
        sock = socket.create_connection(('127.0.0.1', self.port), timeout=3)
        self.addCleanup(sock.close)
        sock.sendall(f'GET /ws HTTP/1.1\r\nHost: 127.0.0.1:{self.port}\r\nOrigin: http://127.0.0.1:{self.port}\r\n\r\n'.encode())
        self.assertTrue(sock.recv(64).startswith(b'HTTP/1.0 400'))

    def test_refused_by_esp(self):
        self.start(accept=False)
        _, data = self.handshake(origin=f'http://127.0.0.1:{self.port}')
        self.assertTrue(data.startswith(b'HTTP/1.0 502'), data[:40])

    def test_esp_unreachable(self):
        spare = socket.socket(); spare.bind(('127.0.0.1', 0)); dead_port = spare.getsockname()[1]; spare.close()
        self.proxy = http.server.ThreadingHTTPServer(('127.0.0.1', 0), make_handler(urllib.parse.urlsplit(f'http://127.0.0.1:{dead_port}')))
        threading.Thread(target=self.proxy.serve_forever, daemon=True).start()
        self.addCleanup(lambda: (self.proxy.shutdown(), self.proxy.server_close()))
        self.port = self.proxy.server_port
        _, data = self.handshake(origin=f'http://127.0.0.1:{self.port}')
        self.assertTrue(data.startswith(b'HTTP/1.0 502'), data[:40])


class ProxyTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.upstream = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Upstream)
        target = urllib.parse.urlsplit(f'http://127.0.0.1:{cls.upstream.server_port}')
        cls.proxy = http.server.ThreadingHTTPServer(('127.0.0.1', 0), make_handler(target))
        for server in (cls.upstream, cls.proxy):
            threading.Thread(target=server.serve_forever, daemon=True).start()

    @classmethod
    def tearDownClass(cls):
        for server in (cls.proxy, cls.upstream):
            server.shutdown()
            server.server_close()

    def request(self, method, path, body=None, headers=None):
        conn = http.client.HTTPConnection('127.0.0.1', self.proxy.server_port, timeout=3)
        conn.request(method, path, body=body, headers=headers or {})
        response = conn.getresponse()
        result = response.status, response.read()
        conn.close()
        return result

    def test_get_and_post(self):
        self.assertEqual(self.request('GET', '/status'), (200, b'bridge'))
        origin = f'http://127.0.0.1:{self.proxy.server_port}'
        self.assertEqual(self.request('POST', '/move', '1,2', {'Origin': origin, 'X-Ronin-Session': '123'}), (200, b'123:1,2'))

    def test_pairing_routes(self):
        origin = f'http://127.0.0.1:{self.proxy.server_port}'
        for path in ('/discover', '/resume', '/disconnect'):
            with self.subTest(path=path):
                self.assertEqual(self.request('POST', path, '', {'Origin': origin}), (200, b':'))
                self.assertEqual(self.request('POST', path, '', {'Origin': 'http://example.com'})[0], 403)
        identity = '00:17:0F:23:B9:1C:48'
        self.assertEqual(self.request('POST', '/select', identity, {'Origin': origin}), (200, b':' + identity.encode()))

    def test_zoom_routes(self):
        origin = f'http://127.0.0.1:{self.proxy.server_port}'
        self.assertEqual(self.request('GET', '/zoom/presets'), (200, b'bridge'))
        for path, body in (('/zoom', '4074,5'), ('/zoom/keepalive', ''), ('/zoom/stop', '')):
            with self.subTest(path=path):
                self.assertEqual(self.request('POST', path, body, {'Origin': origin, 'X-Ronin-Session': '42'}),
                                 (200, b'42:' + body.encode()))
                self.assertEqual(self.request('POST', path, body, {'Origin': 'http://example.com'})[0], 403)

    def test_sleep_route(self):
        origin = f'http://127.0.0.1:{self.proxy.server_port}'
        self.assertEqual(self.request('POST', '/sleep', '0', {'Origin': origin, 'X-Ronin-Session': '42'}), (200, b'42:0'))
        self.assertEqual(self.request('POST', '/sleep', '1', {'Origin': 'http://example.com'})[0], 403)

    def test_rejects_external_origin_host_and_unknown_path(self):
        self.assertEqual(self.request('POST', '/move', '1,2', {'Origin': 'http://example.com'})[0], 403)
        self.assertEqual(self.request('GET', '/status', headers={'Host': 'example.com'})[0], 403)
        self.assertEqual(self.request('GET', '/other')[0], 404)


if __name__ == '__main__':
    unittest.main()
