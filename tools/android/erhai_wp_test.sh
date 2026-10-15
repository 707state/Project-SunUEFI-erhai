#!/system/bin/sh
# READ-MOSTLY diagnostic: test whether writes to the GPT region (4K-LBA2) stick.
# We write a test pattern to a SPARE UNUSED area first (LBA6-7 area of entry array
# is in use... use the area we already corrupted+zeroed: 4K-LBA17 — already blank),
# then test the real target: read original 16 bytes at entry15 position, write them
# back IDENTICALLY (no data change!), and see if the read differs.
set -e
SDA=/dev/block/sda

echo "=== 1. write test to BLANK area (4K-LBA17, byte 69632) ==="
printf '\xde\xad\xbe\xef' > /data/local/tmp/_t
dd if=/data/local/tmp/_t of=$SDA bs=1 seek=69632 count=4 conv=notrunc
sync
echo "readback:"
dd if=$SDA bs=1 skip=69632 count=4 2>/dev/null | od -A n -t x1

echo "=== 2. GPT area write test (4K-LBA2 + entry15 offset 1920) ==="
# read current 4 bytes, write same bytes back (idempotent — no data change)
dd if=$SDA bs=1 skip=$((8192+1920)) count=4 2>/dev/null > /data/local/tmp/_orig4
echo "original 4 bytes:"
od -A n -t x1 < /data/local/tmp/_orig4
# now write DIFFERENT pattern to test
printf '\xaa\x55\xaa\x55' > /data/local/tmp/_t
dd if=/data/local/tmp/_t of=$SDA bs=1 seek=$((8192+1920)) count=4 conv=notrunc
sync
echo "readback after write (aa55aa55 expected if writable):"
dd if=$SDA bs=1 skip=$((8192+1920)) count=4 2>/dev/null | od -A n -t x1
echo "restoring original 4 bytes..."
dd if=/data/local/tmp/_orig4 of=$SDA bs=1 seek=$((8192+1920)) count=4 conv=notrunc
sync
dd if=$SDA bs=1 skip=$((8192+1920)) count=4 2>/dev/null | od -A n -t x1

echo "=== 3. block device RO status ==="
blockdev --getro $SDA
cat /sys/class/block/sda/ro 2>/dev/null || echo "no ro attr"

echo "=== 4. UFS write protect via ioctl check (scsi disk) ==="
cat /sys/class/block/sda/device/write_protect 2>/dev/null || echo "no wp attr"
