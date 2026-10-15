#!/system/bin/sh
# Test EXACTLY the v2 write path: dd bs=4096 seek=2 count=1 full-block write.
# Writes a test block to the ALREADY-BLANK LBA17 first (safe), then reads it back
# via a DIFFERENT path (bs=1 read at computed offset) to catch offset bugs.
set -e
SDA=/dev/block/sda

echo "=== A. full-block write to blank LBA17 via dd bs=4096 seek=17 ==="
head -c 4096 /dev/urandom > /data/local/tmp/_blk.bin
dd if=/data/local/tmp/_blk.bin of=$SDA bs=4096 seek=17 count=1 conv=notrunc
sync
echo "read back via bs=1 at byte 69632 (first 16 bytes):"
dd if=$SDA bs=1 skip=69632 count=16 2>/dev/null | od -A n -t x1
echo "expected (first 16 of random block):"
dd if=/data/local/tmp/_blk.bin bs=1 count=16 2>/dev/null | od -A n -t x1

echo "=== B. read LBA2 via bs=4096 skip=2 (first 16 bytes) vs bs=1 at 8192 ==="
dd if=$SDA bs=4096 skip=2 count=1 2>/dev/null | dd bs=1 count=16 2>/dev/null | od -A n -t x1
dd if=$SDA bs=1 skip=8192 count=16 2>/dev/null | od -A n -t x1

echo "=== C. the REAL v2-style write to LBA2 (idempotent: write back what's there) ==="
dd if=$SDA bs=4096 skip=2 count=1 of=/data/local/tmp/_lba2.bin 2>/dev/null
# flip nothing — write same content back with v2's exact syntax:
dd if=/data/local/tmp/_lba2.bin of=$SDA bs=4096 seek=2 count=1 conv=notrunc
sync
dd if=$SDA bs=4096 skip=2 count=1 2>/dev/null > /data/local/tmp/_lba2_check.bin
cmp -s /data/local/tmp/_lba2.bin /data/local/tmp/_lba2_check.bin && echo "LBA2 roundtrip OK" || echo "LBA2 roundtrip MISMATCH"
echo "also compare entry15 zone (offset 1920-1936):"
dd if=/data/local/tmp/_lba2_check.bin bs=1 skip=1920 count=16 2>/dev/null | od -A n -t x1
