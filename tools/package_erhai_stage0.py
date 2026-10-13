#!/usr/bin/env python3
"""Package the erhai stage-0 FD and BootShim into a fastboot-boot image.

Host-only. The image carries no stock Android kernel: `fastboot boot` loads
it into memory, ABL decompresses the gzip kernel (BootShim + 3 MiB FD), and
the shim copies the FD to 0xA7100000 before entering it. Nothing is flashed.
"""
import argparse
import gzip
import hashlib
import json
import struct
import subprocess
import sys
from pathlib import Path

from build_integrity import validate as validate_build
from firmware_workspace import active_root

ROOT = Path(__file__).resolve().parents[1]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--header-version', type=int, choices=(3, 4), default=4)
    parser.add_argument('--dtb', type=Path,
                        default=Path('vendor/erhai/board.dtb'),
                        help='DTB appended after the gzip kernel (real erhai live FDT)')
    args = parser.parse_args()
    root = active_root(ROOT)
    out = root / 'artifacts/erhai'
    build = validate_build(root, 'erhai')
    fd_bytes = (out / 'erhai-stage0.fd').read_bytes()
    shim_bytes = (out / 'BootShim.bin').read_bytes()
    if len(fd_bytes) != 0x300000 or shim_bytes[56:60] != b'ARM\x64':
        raise ValueError('Unexpected erhai stage-0 FD/BootShim')
    dtb_bytes = args.dtb.read_bytes()
    if struct.unpack_from('>I', dtb_bytes, 0)[0] != 0xD00DFEED:
        raise ValueError('Appended blob is not a DTB')
    # Same recipe as the piano MembootTest product image: gzip(BootShim+FD)
    # plus appended DTB; ABL decompresses the kernel and hands the DTB over.
    kernel = out / 'erhai-stage0-kernel.bin'
    kernel.write_bytes(gzip.compress(shim_bytes + fd_bytes, mtime=0) + dtb_bytes)
    empty_ramdisk = out / 'erhai-stage0-ramdisk.empty'
    empty_ramdisk.write_bytes(b'')
    image = out / 'ErhaiUEFI-stage0.img'
    ws = root / 'upstream/Mu-Silicium'
    subprocess.run([sys.executable, str(ws / 'Resources/Scripts/mkbootimg.py'),
                    '--header_version', str(args.header_version),
                    '--kernel', str(kernel), '--ramdisk', str(empty_ramdisk),
                    '-o', str(image)], check=True)
    blob = image.read_bytes()
    ks, rs = struct.unpack_from('<2I', blob, 8)
    roff = 4096 + ((ks + 4095) // 4096) * 4096
    if blob[:8] != b'ANDROID!' or struct.unpack_from('<I', blob, 40)[0] != args.header_version \
            or blob[4096:4096 + ks] != kernel.read_bytes() \
            or rs != 0 or blob[roff:roff + rs]:
        raise ValueError('Erhai boot image contents mismatch')
    result = {
        'build_id': build['build_id'],
        'status': 'STAGE0_BRINGUP_NOT_PRODUCT',
        'android_header_version': args.header_version,
        'fd_sha256': sha(fd_bytes),
        'bootshim_sha256': sha(shim_bytes),
        'dtb_source': str(args.dtb),
        'dtb_sha256': sha(dtb_bytes),
        'image_bytes': len(blob),
        'image_sha256': sha(blob),
        'notes': 'Standalone stage-0 diagnostic image for fastboot boot; no stock '
                 'kernel, no APP payload, no flash required. DTB is the real erhai '
                 'live FDT (vendor/erhai/board.dtb); memory map remains a '
                 'same-SoC stand-in until an xbl_config capture is prepared.',
    }
    (out / 'erhai-stage0-image.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
    kernel.unlink()
    empty_ramdisk.unlink()


if __name__ == '__main__':
    main()
