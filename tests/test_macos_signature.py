import hashlib
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from verify_macos_signature import verify


class SignatureTests(unittest.TestCase):
    def test_valid_and_modified_code(self):
        identifier = b'test\0'
        hash_offset = 44 + len(identifier)
        directory_size = hash_offset + 32
        signature_size = 20 + directory_size
        header = struct.pack('<8I', 0xfeedfacf, 0x1000007, 3, 2, 1, 16, 0, 0)
        command = struct.pack('<4I', 0x1d, 16, 96, signature_size)
        code = (header + command).ljust(96, b'\0')
        directory = struct.pack('>9I4BI', 0xfade0c02, directory_size, 0x20000, 2,
                                hash_offset, 44, 0, 1, 96, 32, 2, 0, 12, 0)
        directory += identifier + hashlib.sha256(code).digest()
        signature = struct.pack('>5I', 0xfade0cc0, signature_size, 1, 0, 20) + directory
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'binary'
            path.write_bytes(code + signature)
            self.assertEqual(verify(path), 1)
            broken = bytearray(code + signature)
            broken[95] = 1
            path.write_bytes(broken)
            with self.assertRaisesRegex(ValueError, 'has changed'):
                verify(path)
            path.write_bytes(code)
            with self.assertRaisesRegex(ValueError, 'beyond'):
                verify(path)


if __name__ == '__main__':
    unittest.main()
