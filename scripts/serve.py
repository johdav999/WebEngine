"""Loopback-only static server; correct Wasm MIME type and no stale build caching."""
import argparse
import errno
import socket
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


class Handler(SimpleHTTPRequestHandler):
    extensions_map = {**SimpleHTTPRequestHandler.extensions_map, '.wasm': 'application/wasm'}

    def end_headers(self):
        self.send_header('Cache-Control', 'no-store')
        super().end_headers()


class LocalHTTPServer(ThreadingHTTPServer):
    # Windows SO_REUSEADDR can allow two servers to share an occupied port.
    allow_reuse_address = not hasattr(socket, 'SO_EXCLUSIVEADDRUSE')

    def server_bind(self):
        if hasattr(socket, 'SO_EXCLUSIVEADDRUSE'):
            self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
        super().server_bind()


def create_server(directory, port, attempts):
    handler = partial(Handler, directory=str(directory))
    last_port = min(65535, port + attempts - 1)
    for candidate in range(port, last_port + 1):
        try:
            return LocalHTTPServer(('127.0.0.1', candidate), handler)
        except OSError as error:
            unavailable = error.errno in (errno.EADDRINUSE, errno.EACCES, errno.EPERM)
            unavailable |= getattr(error, 'winerror', None) in (10013, 10048)
            if not unavailable or candidate == last_port:
                raise


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--port', type=int, default=8080)
    parser.add_argument('--port-attempts', type=int, default=1,
                        help='Try this many consecutive ports (default: 1).')
    parser.add_argument('--directory', type=Path, default=Path(__file__).resolve().parents[1] / 'dist')
    args = parser.parse_args()
    if not 1 <= args.port <= 65535 or not 1 <= args.port_attempts <= 100:
        parser.error('Port must be 1..65535; port attempts must be 1..100.')
    if not (args.directory / 'index.html').is_file():
        parser.error('No browser build found. Build the project first.')
    try:
        server = create_server(args.directory, args.port, args.port_attempts)
    except OSError as error:
        last_port = min(65535, args.port + args.port_attempts - 1)
        parser.exit(1, f'Cannot start development server on ports {args.port}-{last_port}: {error}\n')
    actual_port = server.server_address[1]
    if actual_port != args.port:
        print(f'Port {args.port} is unavailable; using port {actual_port}.', flush=True)
    print(f'WebEngine: http://localhost:{actual_port} (Ctrl+C to stop)', flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
