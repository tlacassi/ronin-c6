import http.client
import http.server
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

    def test_rejects_external_origin_host_and_unknown_path(self):
        self.assertEqual(self.request('POST', '/move', '1,2', {'Origin': 'http://example.com'})[0], 403)
        self.assertEqual(self.request('GET', '/status', headers={'Host': 'example.com'})[0], 403)
        self.assertEqual(self.request('GET', '/other')[0], 404)


if __name__ == '__main__':
    unittest.main()
