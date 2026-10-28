"""Verify ad-hoc Mach-O CodeDirectory page hashes on Linux.

Layout: Apple's xnu/osfmk/kern/cs_blobs.h. This does not evaluate Gatekeeper,
Developer ID trust, notarization, or the complete bundle resource seal.
"""
import hashlib
from pathlib import Path
import struct


def verify(path):
    data = Path(path).read_bytes()
    if data[:4] != bytes.fromhex('cffaedfe'):
        raise ValueError(f'{path}: expected a thin 64-bit Mach-O')
    command_count, command_bytes = struct.unpack_from('<II', data, 16)
    pos, signature = 32, None
    for _ in range(command_count):
        command, size = struct.unpack_from('<II', data, pos)
        if size < 8 or pos + size > 32 + command_bytes:
            raise ValueError('Invalid Mach-O load command')
        if command == 0x1d:  # LC_CODE_SIGNATURE
            offset, length = struct.unpack_from('<II', data, pos + 8)
            if offset + length > len(data):
                raise ValueError('Signature extends beyond the file')
            signature = data[offset:offset + length]
            signature_offset = offset
        pos += size
    if signature is None:
        raise ValueError(f'{path}: no embedded signature')
    magic, length, count = struct.unpack_from('>III', signature)
    if magic != 0xfade0cc0 or length > len(signature) or 12 + 8 * count > length:
        raise ValueError('Invalid signature superblob')
    blobs = {}
    for i in range(count):
        kind, offset = struct.unpack_from('>II', signature, 12 + 8 * i)
        blob_size = struct.unpack_from('>I', signature, offset + 4)[0]
        if offset + blob_size > length or blob_size < 8:
            raise ValueError('Invalid signature blob')
        blobs[kind] = signature[offset:offset + blob_size]
    directories = [b for b in blobs.values() if b[:4] == bytes.fromhex('fade0c02')]
    if not directories:
        raise ValueError('Missing CodeDirectory')
    for directory in directories:
        _, size, version, flags, hash_offset, _, special, slots, limit = struct.unpack_from('>9I', directory)
        hash_size, hash_type, _, exponent = struct.unpack_from('4B', directory, 36)
        if not flags & 2:
            raise ValueError('Expected an ad-hoc signature')
        if version >= 0x20300 and limit == 0xffffffff:
            limit = struct.unpack_from('>Q', directory, 56)[0]
        if exponent > 30 or limit > signature_offset:
            raise ValueError('Invalid signed code extent')
        page = (1 << exponent) if exponent else limit
        if not page or slots != (limit + page - 1) // page:
            raise ValueError('Invalid code-page count')
        algorithm = {1: 'sha1', 2: 'sha256', 3: 'sha256', 4: 'sha384'}[hash_type]
        digest_size = hashlib.new(algorithm).digest_size
        if hash_size != (20 if hash_type == 3 else digest_size):
            raise ValueError('Invalid digest size')
        if hash_offset < special * hash_size or hash_offset + slots * hash_size > size:
            raise ValueError('Invalid hash table')
        for i in range(slots):
            actual = hashlib.new(algorithm, data[i * page:min((i + 1) * page, limit)]).digest()[:hash_size]
            expected = directory[hash_offset + i * hash_size:hash_offset + (i + 1) * hash_size]
            if actual != expected:
                raise ValueError(f'{path}: code page {i} has changed since signing')
        # Check signature-internal special blobs (requirements, entitlements).
        for kind, blob in blobs.items():
            if 0 < kind <= special:
                expected = directory[hash_offset - kind * hash_size:hash_offset - (kind - 1) * hash_size]
                actual = hashlib.new(algorithm, blob).digest()[:hash_size]
                if actual != expected:
                    raise ValueError(f'{path}: special slot {kind} hash mismatch')
    return len(directories)
