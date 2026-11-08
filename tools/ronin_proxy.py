#!/usr/bin/env python3
"""Optional loopback-only proxy for browsers requiring a secure gamepad context."""
import argparse
import http.client
import http.server
import urllib.parse

GET_PATHS = {'/', '/gamepad.js', '/status', '/devices', '/presets', '/zoom/presets'}
POST_PATHS = {'/move', '/select', '/discover', '/resume', '/disconnect', '/preset/save', '/preset/goto', '/preset/delete', '/zoom', '/zoom/keepalive', '/zoom/stop'}


def make_handler(target):
    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            self.forward('GET')

        def do_POST(self):
            self.forward('POST')

        def forward(self, method):
            port = self.server.server_port
            allowed_hosts = {f'127.0.0.1:{port}', f'localhost:{port}'}
            host = self.headers.get('Host', '')
            if host not in allowed_hosts:
                self.send_error(403)
                return
            if method == 'POST' and self.headers.get('Origin') != f'http://{host}':
                self.send_error(403)
                return
            allowed = GET_PATHS if method == 'GET' else POST_PATHS
            if self.path not in allowed:
                self.send_error(404)
                return
            body = None
            headers = {}
            if method == 'POST':
                try:
                    length = int(self.headers.get('Content-Length', '-1'))
                except ValueError:
                    length = -1
                if not 0 <= length <= 256:
                    self.send_error(400)
                    return
                body = self.rfile.read(length)
                headers['Content-Type'] = self.headers.get('Content-Type', 'text/plain')
                session = self.headers.get('X-Ronin-Session')
                if session is not None:
                    headers['X-Ronin-Session'] = session
            connection = http.client.HTTPConnection(target.hostname, target.port or 80, timeout=2)
            try:
                connection.request(method, self.path, body, headers)
                response = connection.getresponse()
                data = response.read(65537)
                if len(data) > 65536:
                    self.send_error(502)
                    return
                self.send_response(response.status)
                self.send_header('Content-Type', response.getheader('Content-Type', 'text/plain'))
                self.send_header('Cache-Control', 'no-store')
                self.send_header('Content-Length', str(len(data)))
                self.end_headers()
                self.wfile.write(data)
            except (OSError, http.client.HTTPException):
                self.send_error(502, 'ESP inaccessible')
            finally:
                connection.close()

    return Handler


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--esp', default='http://ronin-c6.local', help='ESP URL, e.g. http://192.168.1.50')
    parser.add_argument('--port', type=int, default=8765)
    args = parser.parse_args()
    target = urllib.parse.urlsplit(args.esp)
    if (target.scheme != 'http' or not target.hostname or target.username or target.password or
            target.path not in ('', '/') or target.query or target.fragment):
        parser.error('--esp must be an HTTP origin without a path or credentials')
    try:
        target.port
    except ValueError:
        parser.error('invalid ESP port')
    server = http.server.ThreadingHTTPServer(('127.0.0.1', args.port), make_handler(target))
    print(f'Open http://127.0.0.1:{server.server_port}/ (Ctrl-C to stop)', flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == '__main__':
    main()
