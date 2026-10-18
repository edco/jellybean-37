#!/usr/bin/env python3
#
# Copyright (c) 2026 Ed Coffey
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#

"""
build_vendor_shim.py - Extract specific object files from vendor archives
and bundle them into a thinned shim static library for clean linking.
"""
import sys
import os
import shutil
import subprocess
import argparse

def main():
    parser = argparse.ArgumentParser(description="Build vendor shim archive from SDK .a archives.")
    parser.add_argument("--ar", required=True, help="Path to ar binary (e.g. llvm-ar or pi32v2/bin/ar)")
    parser.add_argument("--output", required=True, help="Output archive path (e.g. libvendor_shim.a)")
    parser.add_argument("--staging-dir", required=True, help="Temporary staging directory for extracted objects")
    parser.add_argument("--extract", nargs="+", required=True,
                        help="Extraction specs in format: <archive_path>:<member_name>[:<renamed_name>]")
    args = parser.parse_args()

    os.makedirs(args.staging_dir, exist_ok=True)
    out_dir = os.path.dirname(os.path.abspath(args.output))
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)

    extracted_objs = []

    ar_binary = os.path.abspath(args.ar)
    for spec in args.extract:
        parts = spec.split(":")
        if len(parts) == 2:
            archive, member = parts
            target_name = member
        elif len(parts) == 3:
            archive, member, target_name = parts
        else:
            sys.exit(f"Invalid extraction spec: {spec}")

        if not os.path.isfile(archive):
            sys.exit(f"Source archive not found: {archive}")

        # Extract specific member into staging dir
        subprocess.check_call([ar_binary, "x", os.path.abspath(archive), member], cwd=args.staging_dir)
        
        extracted_file = os.path.join(args.staging_dir, member)
        target_file = os.path.join(args.staging_dir, target_name)
        if member != target_name and os.path.exists(extracted_file):
            if os.path.exists(target_file):
                os.remove(target_file)
            os.rename(extracted_file, target_file)

        extracted_objs.append(target_file)

    # Repack into target archive with symbol table
    lto_ar = os.path.join(os.path.dirname(args.ar), "lto-ar")
    ar_cmd = lto_ar if os.path.exists(lto_ar) else args.ar
    
    if os.path.exists(args.output):
        os.remove(args.output)
    
    cmd = [ar_cmd, "rcs", os.path.abspath(args.output)] + [os.path.basename(f) for f in extracted_objs]
    subprocess.check_call(cmd, cwd=args.staging_dir)

    print(f"[build_vendor_shim] Generated {args.output} with {len(extracted_objs)} objects.")

if __name__ == "__main__":
    main()
