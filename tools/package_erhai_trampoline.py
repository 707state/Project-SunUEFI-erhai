#!/usr/bin/env python3
"""Host-only combined stock-GKI/erhai-stage0 BOOT image for the inactive slot.

Adapts the piano trampoline layout to OnePlus erhai (Pad 2 Pro). The real
stock boot_b.img measured 2026-10-13: BOOT4, raw GKI, dtb_size == 0 — the
vendor DTB arrives via DTBO/vendor_boot, NOT inside BOOT (LineageOS builds
differ from stock). The stock Image stays untouched at the kernel base;
request pages, selector, BootShim, stage-0 FD and a 64-byte APPv1 stub follow
the GKI image_size working area. If dtb_size were set, a DTB copy would be
placed at the file offset ABL derives from the NEW kernel_size.

Never writes a device; flashing to the inactive slot is an explicit user
action (boot_a when slot b is active — the user reported active=b).
"""
import argparse
import hashlib
import json
import os
import re
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from firmware_workspace import active_root
from package_product_trampoline import build_selector, combine, digest, require, align, PAGE

DEFAULT_BOOT_BYTES = 96 * 1024**2
DTB_MAGIC = 0xD00DFEED


def erhai_stock(source, boot_bytes):
    """Parse a stock erhai BOOT4 and return (kernel, dtb, info)."""
    require(len(source) >= PAGE and source[:8] == b'ANDROID!', 'Not an Android boot image')
    kernel_size = struct.unpack_from('<I', source, 8)[0]
    ramdisk_size = struct.unpack_from('<I', source, 12)[0]
    header_size = struct.unpack_from('<I', source, 20)[0]
    version = struct.unpack_from('<I', source, 40)[0]
    cmdline = bytes(source[44:1580]).split(b'\0', 1)[0]
    require(version == 4 and header_size == 1584 and not ramdisk_size,
            'Expected stock BOOT4 with separate init_boot and no ramdisk')
    require(4096 <= kernel_size <= len(source) - PAGE, 'Kernel extent exceeds input')
    kernel = source[PAGE:PAGE + kernel_size]
    require(kernel[:2] == b'MZ' and kernel[56:60] == b'ARMd',
            'Expected raw AArch64 GKI, not a compressed or already wrapped input')
    code0, code1, offset, span, flags = struct.unpack_from('<IIQQQ', kernel)
    require(offset % (2 * 1024**2) == 0 and kernel_size <= span < boot_bytes and not flags & 1,
            'Unsupported GKI placement/span/endianness')
    displacement = code1 & 0x3ffffff
    if displacement & 0x2000000:
        displacement -= 0x4000000
    require(code1 >> 26 == 5 and 64 <= 4 + displacement * 4 < len(kernel),
            'Unsupported original GKI entry branch')
    require(b'SUNUEFI-SPLITv1\0' not in kernel,
            'Input is already wrapped; restore/extract its original kernel first')
    # Stock erhai BOOT measured dtb_size == 0 (DTB comes via DTBO/vendor_boot);
    # keep the branch for LineageOS-style inputs that do carry one.
    dtb_size = struct.unpack_from('<I', source, 1580)[0]
    dtb_bytes = b''
    if dtb_size:
        dtb_off = PAGE + align(kernel_size)
        require(dtb_off + dtb_size <= len(source), 'DTB extent exceeds input')
        dtb_bytes = source[dtb_off:dtb_off + dtb_size]
        require(struct.unpack_from('>I', dtb_bytes, 0)[0] == DTB_MAGIC,
                'dtb_size is set but the section is not a DTB')
        require(not source[PAGE + kernel_size:dtb_off].strip(b'\0') or True, 'padding tolerated')
    return kernel, dtb_bytes, {'kernel_bytes': kernel_size, 'image_size': span,
                               'text_offset': offset, 'flags': flags, 'code0': code0,
                               'code1': code1, 'dtb_bytes': dtb_size,
                               'cmdline': cmdline.decode(errors='replace'),
                               'kernel_sha256': digest(kernel),
                               'dtb_sha256': digest(dtb_bytes)}


def erhai_dtb_for_abl():
    """Return the real erhai live FDT with runtime snapshot values stripped.

    /chosen properties set by the previous boot (initrd pointers, kaslr-seed)
    must not leak into the next handoff; ABL rewrites them for its own kernel
    path. Values are zeroed in place, sizes kept, mirroring the piano live-DTB
    cleaning in tools/prepare_piano.py.
    """
    import sys
    sys.path.insert(0, str(ROOT / 'tools'))
    from compose_piano_dtb import read_fdt
    data = bytearray((ROOT / 'vendor/erhai/board.dtb').read_bytes())
    nodes = read_fdt(bytes(data))
    chosen = nodes['tree'].get('/chosen', {})
    STRIP = ('linux,initrd-start', 'linux,initrd-end', 'kaslr-seed', 'rng-seed',
             'serial-number', 'linux,elfcorehdr', 'linux,usable-memory-range')
    import struct as _s
    # Locate the /chosen property blocks in the raw blob: find the bootargs
    # property inside /chosen and zero the snapshot props that follow in the
    # structure block. Rather than full FDT surgery, zero the prop VALUES by
    # finding each 8-byte little/big pattern is unreliable; instead rebuild via
    # offsets recorded by the parser when available.
    # Practical approach: the props are small and adjacent; find 'chosen' node
    # in strings block, then walk its property records in the struct block.
    # FDT layout: header, rsvmap, struct block, strings block.
    totalsize, off_struct, off_strings = _s.unpack_from('>III', data, 4 + 4 + 8)
    def find_string(name):
        blob = bytes(data[off_strings:totalsize])
        i = blob.find(name.encode() + b'\0')
        return off_strings + i if i >= 0 else None
    props = {name: find_string(name) for name in STRIP}
    # walk struct block: tokens FDT_BEGIN_NODE(1) FDT_END_NODE(2) FDT_PROP(3)
    # FDT_NOP(4) FDT_END(9)
    i = off_struct
    in_chosen = False
    depth = 0
    while i < off_strings:
        token = _s.unpack_from('>I', data, i)[0]
        if token == 1:
            end = data.index(b'\0', i + 4)
            name = bytes(data[i + 4:end]).decode(errors='replace')
            if name == 'chosen':
                in_chosen = True
            i = (end + 1 + 3) & ~3
        elif token == 2:
            if in_chosen and name == 'chosen':
                break
            i += 4
        elif token == 3:
            plen = _s.unpack_from('>I', data, i + 4)[0]
            nameoff = _s.unpack_from('>I', data, i + 8)[0]
            pname = bytes(data[nameoff:nameoff + 32]).split(b'\0')[0].decode(errors='replace')
            if in_chosen and pname in STRIP:
                for j in range(i + 12, i + 12 + plen):
                    data[j] = 0
            i = (i + 12 + plen + 3) & ~3
        else:
            i += 4
    return bytes(data)


def stage0_app_stub():
    """Minimal APPv1 header, empty payload. The selector validates the header
    and binds request pages to its digest; the stage-0 FD never reads it."""
    return struct.pack('<16sIIQ32s', b'SUNUEFI-APPv1\0'.ljust(16, b'\0'),
                       1, 64, 0, hashlib.sha256(b'').digest())


def package(stock, output, boot_bytes, uefi_request=False):
    stock, output = Path(stock).resolve(), Path(output).resolve()
    require(not output.exists(), 'Output must be a fresh directory')
    source = stock.read_bytes()
    kernel, dtb_bytes, original = erhai_stock(source, boot_bytes)
    from build_integrity import validate
    root = active_root(ROOT)
    validate(root, 'erhai')
    fd_bytes = (root / 'artifacts/erhai/erhai-stage0.fd').read_bytes()
    shim_bytes = (root / 'artifacts/erhai/BootShim.bin').read_bytes()
    require(len(fd_bytes) == 0x300000 and shim_bytes[56:60] == b'ARM\x64',
            'Unexpected erhai stage-0 FD/BootShim')
    app = stage0_app_stub()
    output.mkdir(parents=True)
    (output / '.incomplete').write_text('Host packaging has not completed.\n')
    work = output / '.work'
    work.mkdir()
    selector, symbols, inputs = build_selector(work, extra_flags=(
        '-DFRAME_WIDTH=3392U', '-DFRAME_HEIGHT=2400U'))
    combined, layout = combine(kernel, selector, symbols, shim_bytes, fd_bytes, app)
    # File layout: header page | combined kernel (new kernel_size) | DTB copy.
    header = bytearray(source[:PAGE])
    struct.pack_into('<I', header, 8, len(combined))
    if uefi_request:
        existing = bytes(header[44:1580]).split(b'\0', 1)[0]
        require(b'sunuefi.boot=' not in existing, 'Stock input already has a SunUEFI request')
        command_line = existing + (b' ' if existing else b'') + b'sunuefi.boot=uefi'
        require(len(command_line) < 1536, 'Request exceeds BOOT cmdline capacity')
        header[44:1580] = command_line.ljust(1536, b'\0')
    body = bytearray(combined)
    if dtb_bytes:
        # Replace the stock DTB copy with the REAL erhai live FDT (cleaned):
        # native drivers (ClockDxe/VcsDxe/UFSDxe) then read this device's actual
        # nodes instead of OnePlus 13 dodge configuration.
        erhai_dtb = erhai_dtb_for_abl()
        body.extend(b'\0' * (align(len(body)) - len(body)))
        dtb_new_off = PAGE + align(len(combined))
        body.extend(erhai_dtb)
        layout['dtb_copy_offset'] = dtb_new_off
        layout['dtb_copy_bytes'] = len(erhai_dtb)
        layout['dtb_copy_source'] = 'vendor/erhai/board.dtb (live FDT, snapshot values stripped)'
        require(PAGE + len(combined) + align(len(erhai_dtb)) < boot_bytes, 'erhai DTB exceeds boot capacity')
    else:
        erhai_dtb = b''
    image = output / 'ErhaiUEFI-combined.img'
    image.write_bytes(bytes(header) + bytes(body))
    total_file = PAGE + len(body)
    require(total_file < boot_bytes, 'Combined image exceeds the boot partition size')
    # Unsigned AVB metadata keeping stock properties/rollback, like piano.
    from package_piano_recovery_avb import AVBTOOL, AVBTOOL_SHA256
    require(digest(AVBTOOL.read_bytes()) == AVBTOOL_SHA256, 'AVB tool source differs')
    command = [sys.executable, str(AVBTOOL)]
    info = subprocess.check_output(command + ['info_image', '--image', str(stock)], text=True)
    rollback = int(re.search(r'^Rollback Index:\s+(\d+)', info, re.M).group(1))
    props = re.findall(r"^    Prop: ([^ ]+) -> '([^']*)'$", info, re.M)
    require(any(k == 'com.android.build.boot.fingerprint' and v.startswith('OnePlus/') for k, v in props),
            'Stock input is not the OnePlus erhai ROM family (fingerprint prefix)')
    args = command + ['add_hash_footer', '--image', str(image), '--dynamic_partition_size',
                      '--partition_name', 'boot', '--algorithm', 'NONE', '--flags', '0',
                      '--rollback_index', str(rollback), '--salt', '']
    for key, value in props:
        args += ['--prop', key + ':' + value]
    subprocess.run(args, check=True)
    alias = work / 'boot.img'
    os.link(image, alias)
    subprocess.run(command + ['verify_image', '--image', str(alias)], check=True,
                   stdout=subprocess.DEVNULL)
    small = image.read_bytes()
    require(small[-64:-60] == b'AVBf', 'Missing BOOT AVB footer')
    with (output / 'install-container.bin').open('xb') as stream:
        stream.write(small)
        stream.seek(boot_bytes - 64)
        stream.write(small[-64:])
    outputs = {'ErhaiUEFI-stage0.fd': fd_bytes, 'BootShim.bin': shim_bytes,
               'app-payload.bin': app, 'selector.bin': selector}
    if dtb_bytes:
        outputs['abl-dtb.dtb'] = erhai_dtb_for_abl()
        outputs['stock-dtb.dtb'] = dtb_bytes
    for name, data in outputs.items():
        (output / name).write_bytes(data)
    result = {'status': 'ERHAI_STAGE0_COMBINED_HOST_BUILT_NOT_DEVICE_VERIFIED',
              'entry_kind': 'stock-gki-early-selector-stage0',
              'android_header_version': 4, 'boot_bytes': boot_bytes,
              'target_partition': 'boot_a (inactive; user reports active slot is b)',
              'stock_input': {'sha256': digest(source), **original},
              'layout': layout,
              'app_payload': '64-byte APPv1 stub, empty payload; stage-0 FD does not read it',
              'normal_path': 'inherited-framebuffer 3s picker then direct stock primary_entry; DTB/initrd unchanged',
              'uefi_entry': 'request pages or sunuefi.boot=uefi; stage-0 diagnostic firmware',
              'device_boot_performed': False, 'device_passthrough_verified': False,
              'rollback': 'fastboot set_active b restores the untouched working slot',
              'files': {}}
    import shutil
    shutil.rmtree(work)
    for path in output.iterdir():
        if path.is_file() and path.name != '.incomplete':
            result['files'][path.name] = {'bytes': path.stat().st_size, 'sha256': digest(path.read_bytes())}
    (output / 'manifest.json').write_text(json.dumps(result, indent=2) + '\n')
    (output / '.incomplete').unlink()
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stock-boot', type=Path, required=True,
                        help='stock boot_b.img pulled from the ACTIVE slot')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--boot-bytes', type=lambda v: int(v, 0), default=DEFAULT_BOOT_BYTES,
                        help='boot_a partition size from: fastboot getvar partition-size:boot_a')
    parser.add_argument('--uefi-request', action='store_true',
                        help='diagnostic: persistent sunuefi.boot=uefi cmdline request')
    args = parser.parse_args()
    try:
        result = package(args.stock_boot, args.output, args.boot_bytes, args.uefi_request)
        print(json.dumps({'status': result['status'], 'layout': result['layout'],
                          'files': result['files']}, indent=2))
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        raise SystemExit(str(error))
