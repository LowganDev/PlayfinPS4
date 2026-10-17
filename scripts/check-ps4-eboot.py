#!/usr/bin/env python3
"""Fails when initialised data of the PS4 ELF did not make it into eboot.bin.

create-fself (OpenOrbis) dropped the contents of a data section once (the static of an inline function): the
console then ran with zeros where the program had values. Every allocated, non-empty PROGBITS section with non-zero
bytes must be found in eboot.bin. Usage: check-ps4-eboot.py ELF EBOOT
"""
import re
import struct
import sys


def sections(elf):
    shoff, = struct.unpack_from("<Q", elf, 0x28)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", elf, 0x3A)
    headers = [struct.unpack_from("<IIQQQQIIQQ", elf, shoff + i * shentsize) for i in range(shnum)]
    names = headers[shstrndx]
    for name, kind, flags, addr, offset, size, *_ in headers:
        end = elf.index(b"\0", names[4] + name)
        yield elf[names[4] + name:end].decode(), kind, flags, addr, offset, size


def relocated(elf):
    """Virtual addresses of the 8-byte slots the dynamic linker fills in at load time (their file bytes may
    legitimately differ in eboot.bin)."""
    slots = set()
    rela = 4
    for _name, kind, _flags, _addr, offset, size in sections(elf):
        if kind != rela:
            continue
        for i in range(0, size, 24):
            slots.add(struct.unpack_from("<Q", elf, offset + i)[0])
    return slots


def main():
    elf = open(sys.argv[1], "rb").read()
    eboot = open(sys.argv[2], "rb").read()
    progbits, alloc, execute = 1, 0x2, 0x4
    slots = relocated(elf)
    missing = []
    checked = 0
    for name, kind, flags, addr, offset, size in sections(elf):
        if kind != progbits or not flags & alloc or flags & execute or size == 0:
            continue
        length = min(size, 4096)
        content = elf[offset:offset + length]
        if not any(content):
            continue
        # the expected bytes, with every load-time relocation slot as a wildcard
        pattern = b""
        i = 0
        while i < length:
            if addr + i in slots:
                pattern += b"." * min(8, length - i)
                i += 8
            else:
                pattern += re.escape(content[i:i + 1])
                i += 1
        checked += 1
        if re.search(pattern, eboot, re.DOTALL) is None:
            missing.append(name)
    if missing:
        print("eboot.bin lacks the initialised data of: " + ", ".join(missing))
        return 1
    print(f"eboot data OK: {checked} initialised sections present")
    return 0


if __name__ == "__main__":
    sys.exit(main())
