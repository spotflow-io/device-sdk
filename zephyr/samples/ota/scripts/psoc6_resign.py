#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Re-sign a PSoC6 RAM-load image for MCUboot RAM_LOAD_REVERT.

imgtool's CLI only accepts --max-align up to 32 and defaults to erase
value 0xFF. PSoC6 flash uses 512-byte rows (erase value 0x00), and
MCUboot sets CONFIG_MCUBOOT_BOOT_MAX_ALIGN=512 from the flash DT. The
boot trailer magic is therefore the max-align-encoded form; a standard
8-byte-align trailer is treated as invalid and the slot is erased.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--in-bin", required=True, type=Path)
	parser.add_argument("--out-bin", required=True, type=Path)
	parser.add_argument("--out-hex", required=True, type=Path)
	parser.add_argument("--key", required=True, type=Path)
	parser.add_argument("--header-size", type=lambda v: int(v, 0), default=0x2000)
	parser.add_argument("--slot-size", type=lambda v: int(v, 0), default=0xE8000)
	parser.add_argument("--load-addr", type=lambda v: int(v, 0), default=0x08000000)
	parser.add_argument("--hex-addr", type=lambda v: int(v, 0), default=0x10020000)
	parser.add_argument("--align", type=int, default=8)
	parser.add_argument("--max-align", type=int, default=512)
	parser.add_argument("--erased-val", default="0")
	parser.add_argument("--version", default="0.0.0+0")
	parser.add_argument("--confirm", action="store_true", default=True)
	args = parser.parse_args()

	from imgtool.image import Image
	from imgtool.keys import load as load_key
	from imgtool.version import decode_version

	key = load_key(str(args.key))
	img = Image(
		version=decode_version(args.version),
		header_size=args.header_size,
		pad_header=False,
		pad=True,
		confirm=args.confirm,
		align=args.align,
		slot_size=args.slot_size,
		max_align=args.max_align,
		erased_val=args.erased_val,
		load_addr=args.load_addr,
	)
	img.load(str(args.in_bin))
	img.create(key, "hash", None)
	args.out_bin.parent.mkdir(parents=True, exist_ok=True)
	# Hex save expects an unpadded payload and writes the trailer itself.
	img.save(str(args.out_hex), hex_addr=args.hex_addr)
	# Bin save pads the payload to the slot size (includes trailer).
	img.save(str(args.out_bin))
	print(
		f"Signed {args.in_bin.name} -> {args.out_bin.name} / {args.out_hex.name} "
		f"(max_align={args.max_align}, erased_val={args.erased_val}, "
		f"hex_addr=0x{args.hex_addr:X}, size={len(img.payload)})"
	)
	return 0


if __name__ == "__main__":
	sys.exit(main())
