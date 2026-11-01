"""Exercise packaged NSS's AIA downloader before adopting it in the application.

Requires vfychain from Debian's libnss3-tools (or an equivalent NSS package).
Only loopback HTTP is used. Each case starts a fresh NSS process and uses an
explicit test trust anchor, so downloaded issuers cannot inherit trust or cache
state from an earlier case. This tests chain building, not a TLS handshake.
"""
import argparse
import http.server
from pathlib import Path
import shutil
import ssl
import subprocess
import threading
import unittest


CERTIFICATES = Path(__file__).resolve().parent / 'certificates'
VFYCHAIN = 'vfychain'


class IssuerServer(http.server.ThreadingHTTPServer):
    allow_reuse_address = True
    daemon_threads = True

    def __init__(self):
        # This address is encoded in the existing test leaf's AIA extension.
        super().__init__(('127.0.0.1', 18081), IssuerHandler)
        self.requests = []
        self.response = ssl.PEM_cert_to_DER_cert(
            (CERTIFICATES / 'issuer.pem').read_text())
        self.status = 200


class IssuerHandler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_args):
        pass

    def do_GET(self):
        self.server.requests.append(self.path)
        self.send_response(self.server.status)
        self.send_header('Content-Type', 'application/pkix-cert')
        self.send_header('Content-Length', str(len(self.server.response)))
        self.end_headers()
        self.wfile.write(self.server.response)


class NssAiaProbe(unittest.TestCase):
    def setUp(self):
        self.server = IssuerServer()
        self.worker = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.worker.start()

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.worker.join()

    def verify(self, *, fetch=True, date='2610311200Z', anchor='root.pem'):
        # -pp selects CERT_PKIXVerifyCert, -u 1 requests TLS server usage,
        # -f enables AIA. A fixed date keeps fixture validity reproducible.
        command = [VFYCHAIN, '-pp', '-u', '1', '-b', date]
        if fetch:
            command.append('-f')
        command += ['-a', str(CERTIFICATES / 'leaf.pem'),
                    '-t', '-a', str(CERTIFICATES / anchor)]
        return subprocess.run(command, capture_output=True, text=True, timeout=20)

    def test_missing_issuer_without_fetching(self):
        result = self.verify(fetch=False)
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.server.requests, [])

    def test_downloads_and_validates_missing_issuer(self):
        result = self.verify()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.server.requests, ['/issuer.der'])

    def test_rejects_malformed_issuer(self):
        self.server.response = b'not a certificate'
        result = self.verify()
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.server.requests, ['/issuer.der'])

    def test_rejects_unrelated_certificate(self):
        self.server.response = ssl.PEM_cert_to_DER_cert(
            (CERTIFICATES / 'root.pem').read_text())
        result = self.verify()
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.server.requests, ['/issuer.der'])

    def test_rejects_http_error(self):
        self.server.status = 404
        result = self.verify()
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.server.requests, ['/issuer.der'])

    def test_rejects_expired_certificate(self):
        result = self.verify(date='4010311200Z')
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--vfychain', default='vfychain', help='NSS vfychain executable')
    args = parser.parse_args()
    VFYCHAIN = shutil.which(args.vfychain)
    if not VFYCHAIN:
        parser.error('vfychain not found; install libnss3-tools or pass --vfychain')
    unittest.main(argv=[__file__], verbosity=2)
