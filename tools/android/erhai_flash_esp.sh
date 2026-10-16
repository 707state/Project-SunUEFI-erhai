#!/system/bin/sh
# Flash the prebuilt 512MiB FAT32 ESP image to sda16.
set -e
echo "flashing ESP image (512MiB) to /dev/block/sda16 ..."
dd if=/data/local/tmp/sunuefi_esp.img of=/dev/block/sda16 bs=4096 conv=notrunc
sync
echo "verify first 16 bytes (expect eb 58 90 ...):"
dd if=/dev/block/sda16 bs=1 count=16 2>/dev/null | od -A n -t x1
echo "DONE"
