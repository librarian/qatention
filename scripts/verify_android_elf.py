#!/usr/bin/env python3
"""Check APK ELF load mappings and version dependencies as seen by the loader.

Use program headers, not section headers: readelf's section-based version dump
can look correct even when Android cannot map the dynamic string table.
"""
import struct
import sys
import zipfile


def verify(data):
    if data[:6] != b'\x7fELF\x02\x01':
        raise ValueError('expected little-endian ELF64')
    phoff = struct.unpack_from('<Q', data, 32)[0]
    phsize, count = struct.unpack_from('<HH', data, 54)
    headers = [struct.unpack_from('<IIQQQQQQ', data, phoff + i * phsize)
               for i in range(count)]
    loads = [p for p in headers if p[0] == 1]
    for _, _, offset, address, _, size, _, alignment in loads:
        if alignment < 4096 or (address - offset) % alignment:
            raise ValueError(f'invalid PT_LOAD mapping: offset={offset:#x}, '
                             f'address={address:#x}, alignment={alignment:#x}')
        if offset + size > len(data):
            raise ValueError('PT_LOAD extends beyond file')

    def file_offset(address):
        for p in loads:
            if p[3] <= address < p[3] + p[5]:
                return p[2] + address - p[3]
        raise ValueError(f'unmapped virtual address {address:#x}')

    dynamic = next(p for p in headers if p[0] == 2)
    tags = {}
    for offset in range(dynamic[2], dynamic[2] + dynamic[5], 16):
        tag, value = struct.unpack_from('<QQ', data, offset)
        if tag == 0:
            break
        tags.setdefault(tag, []).append(value)
    strings = file_offset(tags[5][0])
    string_size = tags[10][0]

    def string(index):
        if index >= string_size:
            raise ValueError('dynamic string index out of bounds')
        end = data.index(b'\0', strings + index, strings + string_size)
        return data[strings + index:end].decode('ascii')

    needed = {string(index) for index in tags.get(1, [])}
    if 0x6ffffffe in tags:
        offset = file_offset(tags[0x6ffffffe][0])
        for _ in range(tags[0x6fffffff][0]):
            _, _, name, _, next_offset = struct.unpack_from('<HHIII', data, offset)
            dependency = string(name)
            if dependency not in needed:
                raise ValueError(f'version dependency {dependency!r} absent from DT_NEEDED')
            offset += next_offset
    return needed, string(tags[14][0]) if 14 in tags else None


def main():
    with zipfile.ZipFile(sys.argv[1]) as apk:
        names = [n for n in apk.namelist() if n.startswith('lib/') and n.endswith('.so')]
        if not names:
            raise ValueError('APK contains no native libraries')
        for name in names:
            try:
                needed, soname = verify(apk.read(name))
                base = name.rsplit('/', 1)[-1]
                if base in ('libcrypto_3.so', 'libssl_3.so'):
                    if soname != base:
                        raise ValueError(f'incorrect OpenSSL SONAME: {soname}')
                    if base == 'libssl_3.so' and 'libcrypto_3.so' not in needed:
                        raise ValueError('libssl does not depend on bundled libcrypto_3.so')
            except (ValueError, struct.error) as error:
                raise ValueError(f'{name}: {error}') from error
        print(f'Validated load mappings and version dependencies in {len(names)} libraries')


if __name__ == '__main__':
    main()
