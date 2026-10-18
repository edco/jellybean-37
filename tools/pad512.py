#!/usr/bin/env python3
#
# Copyright (c) 2026 Ed Coffey
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#

import sys

def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <binary_file> [alignment]", file=sys.stderr)
        sys.exit(1)
    
    filepath = sys.argv[1]
    align = int(sys.argv[2], 0) if len(sys.argv) > 2 else 512

    with open(filepath, 'ab+') as f:
        f.seek(0, 2)
        size = f.tell()
        rem = size % align
        if rem != 0:
            pad = align - rem
            f.write(b'\x00' * pad)
            print(f"[pad512] Padded {filepath} from {size} to {size + pad} bytes (+{pad})")
        else:
            print(f"[pad512] {filepath} already aligned to {align} bytes ({size} bytes)")

if __name__ == '__main__':
    main()
