#!/usr/bin/env python3
"""Fails when the PS4 executable links a third-party library that resources/licenses/manifest.txt does not cover
(its licence would then be missing from the package). Usage: check-ps4-licenses.py <link.txt> <manifest.txt>"""
import re
import sys

# the console's own system modules and Playfin's own libraries
SYSTEM = re.compile(r"^(kernel|Sce.*|m|pthread)$")
OWN = re.compile(r"^playfin_.*$")


def linked(link_txt):
    libraries = set()
    for token in open(link_txt).read().split():
        if token.startswith("-l"):
            libraries.add(token[2:])
        elif token.endswith(".a"):
            name = token.rsplit("/", 1)[-1][:-2]
            libraries.add(name[3:] if name.startswith("lib") else name)
    return libraries


def covered(manifest):
    libraries = set()
    for line in open(manifest, encoding="utf-8"):
        line = line.rstrip("\n")
        if not line or line.startswith("#"):
            continue
        fields = line.split("|")
        if len(fields) == 5:
            libraries.update(name for name in fields[4].split(",") if name)
    return libraries


def main():
    link_txt, manifest = sys.argv[1], sys.argv[2]
    missing = sorted(lib for lib in linked(link_txt) - covered(manifest) if not SYSTEM.match(lib) and not OWN.match(lib))
    if missing:
        print("PS4 libraries without a licence in resources/licenses/manifest.txt: " + ", ".join(missing))
        return 1
    print("PS4 licences OK: every linked library is in the manifest")
    return 0


sys.exit(main())
