#!/bin/bash
# Stage kernel artifacts into the ESP image (build/sunuefi_esp.img).
# Expects: build/kernels/erhai-first Image+DTB, build/erhai-initramfs.cpio.gz.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
ESP="$ROOT/build/sunuefi_esp.img"
K="$ROOT/build/kernels/erhai-first"
INITRD="$ROOT/build/erhai-initramfs.cpio.gz"
test -f "$ESP" && test -f "$K/arch/arm64/boot/Image" && test -f "$INITRD"
M=$(mktemp -d)
hdiutil attach "$ESP" -mountpoint "$M" -quiet
trap 'hdiutil detach "$M" -quiet >/dev/null 2>&1 || true; rm -rf "$M"' EXIT
mkdir -p "$M/EFI/BOOT"
cp "$K/arch/arm64/boot/Image" "$M/EFI/BOOT/BOOTAA64.EFI"
cp "$K/arch/arm64/boot/dts/qcom/sm8750-oneplus-erhai.dtb" "$M/EFI/BOOT/erhai.dtb"
cp "$INITRD" "$M/EFI/BOOT/initramfs.cpio.gz"
rm -rf "$M/.fseventsd" "$M"/._* "$M/EFI/BOOT"/._* 2>/dev/null || true
sync
hdiutil detach "$M" -quiet
echo "staged: BOOTAA64.EFI + erhai.dtb + initramfs.cpio.gz -> $ESP"
