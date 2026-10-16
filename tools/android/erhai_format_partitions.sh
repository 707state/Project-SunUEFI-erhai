#!/system/bin/sh
# Format sunuefi_esp (sda16, FAT32) and sunuefi_root (sda17, ext4).
# Both partitions are freshly carved — no data to lose.
set -e
echo "=== ESP (sda16) -> FAT32 ==="
# FAT32 on a 512MiB partition: mkfs.fat -F 32. Use label SUNUEFI_ESP.
mkfs.fat -F 32 -n SUNUEFI_ESP /dev/block/sda16 2>/dev/null \
  || make_fat32 /dev/block/sda16 2>/dev/null \
  || { echo "no mkfs.fat on device — will format from UEFI side or host"; }

echo "=== ROOT (sda17) -> ext4 ==="
mke2fs -t ext4 -L sunuefi_root /dev/block/sda17 2>/dev/null \
  || mkfs.ext4 -L sunuefi_root /dev/block/sda17 2>/dev/null \
  || { echo "no mkfs.ext4 on device — will format later from Linux/UEFI side"; }

echo "=== verify ==="
blkid /dev/block/sda16 /dev/block/sda17 2>/dev/null || true
echo "DONE"
