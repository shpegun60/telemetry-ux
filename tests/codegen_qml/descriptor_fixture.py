"""Explicit UX-owned descriptor test data; no SDK parser or native model copy.

Derived from the former SDK test-only wire fixtures at
6e15220b1271139b3f1996a16d385c660f856f45. Production
Descriptor validation and generation are exercised through the installed SDK.
Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
"""
import struct


def u32(*values):
    return struct.pack('<' + 'I' * len(values), *values)


def text(value):
    encoded = value.encode('utf-8')
    return u32(len(encoded)) + encoded


def record(kind, payload):
    return struct.pack('<BBHI', kind, 1, 0, len(payload)) + payload


def type_record(index, kind, wire, tail=b''):
    return record(1, u32(index) + bytes((kind, 0, 0, 0)) + u32(wire) + tail)


def struct_type(index, wire, members):
    return type_record(index, 3, wire, u32(len(members)) + b''.join(
        u32(kind) + text(name) for kind, name in members))


def enum_type(index, base, width, entries):
    return type_record(index, 2, width, u32(base, len(entries)) + b''.join(
        (code & ((1 << (8 * width)) - 1)).to_bytes(width, 'little') + text(name)
        for code, name in entries))


def descriptor(extra=(), groups=((), (), ())):
    types = [type_record(0, 0, 0)] + [
        type_record(code, 1, size, bytes((code, 0, 0, 0)))
        for code, size in enumerate((1, 1, 1, 2, 2, 4, 4, 8, 8, 4, 8), 1)]
    types.extend(extra)
    catalogs, endpoints, counts = [], [], []
    for category, entries_by_catalog in enumerate(groups, 1):
        first = 0
        for group, (name, entries) in enumerate(entries_by_catalog):
            catalogs.append(record(2, bytes((category, 0, 0, 0))
                                   + u32(group, first, len(entries)) + text(name)))
            for local, entry in enumerate(entries):
                primary, name, *rest = entry
                payload = u32(group << 16 | local, primary)
                if category == 1:
                    payload += bytes((rest[0] if rest else 1, 0, 0, 0))
                elif category == 3:
                    payload += u32(rest[0])
                endpoints.append(record(category + 2, payload + text(name)))
            first += len(entries)
        counts.extend((len(entries_by_catalog), first))
    types, catalogs, endpoints = b''.join(types), b''.join(catalogs), b''.join(endpoints)
    total = 64 + len(types) + len(catalogs) + len(endpoints)
    header = b'TDS3' + struct.pack('<4HIQ10I', 3, 0, 64, 0, total, 0, 12 + len(extra),
                                  *counts, 64, 64 + len(types), 64 + len(types) + len(catalogs))
    data = bytearray(header + types + catalogs + endpoints)
    fingerprint = 0xcbf29ce484222325
    for byte in data:  # The fingerprint header field is still zero here.
        fingerprint = ((fingerprint ^ byte) * 0x100000001b3) & 0xffffffffffffffff
    struct.pack_into('<Q', data, 16, fingerprint)
    return bytes(data)


def fixture(name):
    if name == 'empty':
        return descriptor()
    if name != 'mixed':
        raise ValueError('Unknown explicit UX fixture')
    extra = [enum_type(12, 2, 1, [(0, 'Off'), (1, 'Run'), (2, 'Fault')]),
             type_record(13, 4, 6, u32(4, 3)),
             struct_type(14, 5, [(10, 'volts'), (1, 'active')]),
             struct_type(15, 7, [(10, 'target'), (4, 'rpm'), (1, 'enabled')])]
    fields = [('motor', [(1, 'Enabled', 1), (4, 'RPM', 1), (10, 'Temperature', 1),
                         (11, 'Precise', 1), (12, 'Mode', 1), (13, 'Samples', 1),
                         (14, 'State', 1), (15, 'Config', 3)])]
    commands = [('motor', [(15, 'Configure'), (0, 'Reset')])]
    services = [('motor', [(15, 'Echo', 15)])]
    return descriptor(extra, (fields, commands, services))
