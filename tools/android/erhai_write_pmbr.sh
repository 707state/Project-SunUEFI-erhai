#!/system/bin/sh
# Write the protective MBR to 4K-LBA0 of sda. The block was all zeros before
# (verified in inspection); this makes PartitionDxe recognize the GPT disk.
set -e
SDA=/dev/block/sda
echo "=== backup LBA0 ==="
dd if=$SDA bs=4096 skip=0 count=1 of=/data/local/tmp/erhai_lba0_orig.bin
echo "=== verify LBA0 is still the zeros we saw ==="
dd if=$SDA bs=1 count=8 2>/dev/null | od -A n -t x1
echo "=== write protective MBR ==="
dd if=/data/local/tmp/erhai-pmbr.bin of=$SDA bs=4096 seek=0 count=1 conv=notrunc
sync
echo "=== verify ==="
dd if=$SDA bs=1 skip=446 count=16 2>/dev/null | od -A n -t x1
echo "expect: 00 00 00 00 ee ff ff ff 01 00 00 00 ff ff ff ff"
dd if=$SDA bs=1 skip=510 count=2 2>/dev/null | od -A n -t x1
echo "expect: 55 aa"
echo DONE
