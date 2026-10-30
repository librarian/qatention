"""CLI must finish web logout after capture and after virtual-media detachment."""
import contextlib
import http.server
import os
from pathlib import Path
import socketserver
import struct
import subprocess
import sys
import tempfile
import threading
import unittest

EXECUTABLE = sys.argv[1]


def read_exact(connection, length):
    data = b''
    while len(data) < length:
        part = connection.recv(length - len(data))
        if not part:
            raise EOFError
        data += part
    return data


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


class ConsolePeer(socketserver.BaseRequestHandler):
    def handle(self):
        self.request.settimeout(5)
        try:
            self.request.sendall(b'RFB 003.008\n')
            read_exact(self.request, 12)
            self.request.sendall(b'\x01\x10' + bytes(24))  # ATEN security challenge.
            read_exact(self.request, 49)
            initialization = bytearray(36)
            struct.pack_into('>HH', initialization, 0, 2, 2)
            self.request.sendall(bytes(4) + initialization)
            read_exact(self.request, 22)
            # One full Hermon RGB555 rectangle, 2x2 black pixels.
            rectangle = bytearray(24)
            struct.pack_into('>H', rectangle, 2, 1)
            struct.pack_into('>HH', rectangle, 8, 2, 2)
            struct.pack_into('>I', rectangle, 20, 18)
            self.request.sendall(rectangle + b'\x01' + bytes(17))
            while self.request.recv(4096):
                pass
        except (EOFError, OSError):
            pass


class MediaPeer(socketserver.BaseRequestHandler):
    def handle(self):
        self.request.settimeout(5)
        try:
            while True:
                header = read_exact(self.request, 8)
                if header[1] == 0x80:  # Vendor's fixed 246-byte attachment record.
                    read_exact(self.request, 238)
                    self.request.sendall(struct.pack('>I', 2) + struct.pack('<I', 1) + b'\0')
                    continue
                command = struct.unpack_from('>I', header)[0]
                read_exact(self.request, struct.unpack_from('<I', header, 4)[0])
                payload = b''
                if command == 8:
                    response, payload = 9, bytes.fromhex('03ffffff')
                elif command == 10:
                    response = 11
                elif command == 5:
                    self.server.detached = True
                    response = 6
                else:
                    continue  # Device configuration, no reply required.
                self.request.sendall(struct.pack('>I', response) + struct.pack('<I', len(payload)) + payload)
        except (EOFError, OSError):
            pass


@contextlib.contextmanager
def running(server):
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        yield server
    finally:
        server.shutdown()
        server.server_close()
        thread.join()


class ShutdownTests(unittest.TestCase):
    def exercise(self, mount):
        routes = []
        logout_after_detach = []
        with running(Server(('127.0.0.1', 0), ConsolePeer)) as console, \
                running(Server(('127.0.0.1', 0), MediaPeer)) as media:
            media.detached = False

            class WebPeer(http.server.BaseHTTPRequestHandler):
                def log_message(self, *_args):
                    pass

                def do_POST(self):
                    self.rfile.read(int(self.headers.get('Content-Length', 0)))
                    self.respond(True)

                def do_GET(self):
                    self.respond(False)

                def respond(self, login):
                    routes.append(self.path)
                    if self.path == '/cgi/logout.cgi':
                        logout_after_detach.append(media.detached)
                    arguments = ['127.0.0.1', 'token', 'pass', 'host',
                                 str(console.server_address[1]), str(media.server_address[1]),
                                 '1', '1', '0', '5900']
                    launch = '<jnlp><application-desc>' + ''.join(
                        '<argument>' + value + '</argument>' for value in arguments
                    ) + '</application-desc></jnlp>'
                    body = b'OK' if login else launch.encode()
                    self.send_response(200)
                    if login:
                        self.send_header('Set-Cookie', 'SID=fixture; Path=/')
                    self.send_header('Content-Length', str(len(body)))
                    self.end_headers()
                    self.wfile.write(body)

            with running(http.server.ThreadingHTTPServer(('127.0.0.1', 0), WebPeer)) as web, \
                    tempfile.TemporaryDirectory() as work:
                args = [EXECUTABLE, '--http', '--host', '127.0.0.1', '--user', 'fixture',
                        '--web-port', str(web.server_port), '--output', str(Path(work) / 'screen.bmp')]
                if mount:
                    image = Path(work) / 'fixture.iso'
                    image.write_bytes(bytes(2048))
                    args += ['--mount-iso', str(image), '--timeout', '1']
                result = subprocess.run(args, input='fixture\n', text=True, capture_output=True,
                                        timeout=10, env=os.environ.copy())
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn('/cgi/logout.cgi', routes)
                if mount:
                    self.assertTrue(media.detached)
                    self.assertEqual(logout_after_detach, [True])

    def test_capture_logs_out(self):
        self.exercise(False)

    def test_unmount_precedes_logout(self):
        self.exercise(True)


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0]])
