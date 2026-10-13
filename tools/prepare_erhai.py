#!/usr/bin/env python3
"""Regenerate the erhai platform snapshot from a captured OnePlus Pad 2 Pro.

No device commands. Mirrors prepare_piano.py: the capture directory must
contain manifest.json, xbl_config_a.img, uefi_a.img, live.dtb and dtbo_a.img
taken from an erhai device (ro.product.device == 'erhai'). Until this runs
against real data, uefi/platforms/erhaiPkg holds same-SoC stand-ins derived
from the pinned upstream dodgePkg (OnePlus 13 / SM8750-AB).
"""
import argparse
import hashlib
import json
from pathlib import Path

from analyze_capture import parse_fdt, reg_ranges, scan_fdts, text

CAPTURE_FILES = ('xbl_config_a.img', 'uefi_a.img', 'live.dtb', 'dtbo_a.img')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--capture', type=Path, required=True)
    ap.add_argument('--source', type=Path, default=Path('uefi/platforms/erhaiPkg'))
    args = ap.parse_args()
    manifest = json.loads((args.capture / 'manifest.json').read_text())
    if manifest['properties']['ro.product.device'] != 'erhai':
        raise SystemExit('Capture is not erhai')
    for name in CAPTURE_FILES:
        actual = hashlib.sha256((args.capture / name).read_bytes()).hexdigest()
        if actual != manifest['files'][name]['sha256']:
            raise SystemExit(f'Capture hash mismatch: {name}')
    fdts = list(scan_fdts((args.capture / 'xbl_config_a.img').read_bytes()))
    config = [nodes for _, _, nodes in fdts
              if '/soc/memorymap' in nodes and '/sw/uefi/int_param' in nodes]
    if len(config) != 1:
        raise SystemExit('Expected one native UEFI configuration DTB')
    nodes = config[0]
    # Same explicit Qualcomm attribute conversion as prepare_piano.py;
    # unknown values must fail instead of being guessed.
    hobs = {0: 'AddMem', 4: 'NoHob', 5: 'AddDev'}
    cache = {2: 'WRITE_BACK', 0x22: 'WRITE_BACK_XN', 0x20: 'WRITE_THROUGH_XN',
             0x25: 'UNCACHED_UNBUFFERED_XN', 9: 'NS_DEVICE'}
    entries = []
    observed = []
    for path, props in nodes.items():
        if not path.startswith(('/soc/memorymap/memory@', '/soc/registermap/memory@')):
            continue
        label = text(props['mem-label'])[0]
        if label == 'NOMAP':
            continue
        r, = reg_ranges(nodes, path)
        hob = hobs[int.from_bytes(props['build-hob'], 'big')]
        attr = cache[int.from_bytes(props['cache-attributes'], 'big')]
        resource = int.from_bytes(props['resource-type'], 'big')
        capability = int.from_bytes(props['resource-attribute'], 'big')
        memory = int.from_bytes(props['memory-type'], 'big')
        observed.append({'name': label, **r, 'hob': hob, 'resource_type': resource,
                         'resource_attribute': capability, 'memory_type': memory, 'arm_attribute': attr})
        if label == 'UEFI_FD':
            # Keep the product FD placement used by BootShim and build_stage0.sh.
            r = {'base': 0xA7100000, 'size': 0x300000}
        if len(label.encode()) >= 32 or r['base'] % 4096 or r['size'] % 4096:
            raise SystemExit(f'Invalid memory descriptor {label}')
        entries.append(f'{{"{label}", 0x{r["base"]:X}, 0x{r["size"]:X}, {hob}, '
                       f'{resource}, 0x{capability:X}, {memory}, {attr}}}')
    source = '// Generated from erhai native xbl_config_a capture.\n'
    source += '#include <Library/MemoryMapLib.h>\n'
    source += 'STATIC EFI_MEMORY_REGION_DESCRIPTOR gMemoryDescriptor[] = {\n  ' + ',\n  '.join(entries) + '\n};\n'
    source += 'VOID GetMemoryMap (OUT EFI_MEMORY_REGION_DESCRIPTOR **Map, OUT UINT8 *Count) {\n'
    source += '  *Map = gMemoryDescriptor; *Count = ARRAY_SIZE (gMemoryDescriptor);\n}\n'
    (args.source / 'Library/MemoryMapLib/MemoryMapLib.c').write_text(source)
    (args.source / 'native-memory-map.json').write_text(json.dumps(observed, indent=2) + '\n')
    print(f'Regenerated {args.source}/Library/MemoryMapLib from capture '
          f'({len(observed)} regions). Update ConfigurationMapLib and the dsc '
          f'identity only after reviewing the capture.')


if __name__ == '__main__':
    main()
