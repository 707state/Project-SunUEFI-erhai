#!/system/bin/sh
# Check if v2 patch landed at 4K-LBA2 (byte 8192):
# entry14 last (offset 14*128+40=1832) should be 0x...; entry15 type at 1920 should be EFI GUID
dd if=/dev/block/sda bs=4096 skip=2 count=1 2>/dev/null > /data/local/tmp/e2.bin
echo "entry15 type GUID (first 16 bytes):"
dd if=/data/local/tmp/e2.bin bs=1 skip=1920 count=16 2>/dev/null | od -A n -t x1
echo "entry14 last LBA bytes (offset 1832, 8 bytes):"
dd if=/data/local/tmp/e2.bin bs=1 skip=1832 count=8 2>/dev/null | od -A n -t x1
echo "entry15 name (offset 1976):"
dd if=/data/local/tmp/e2.bin bs=1 skip=1976 count=24 2>/dev/null | od -A n -c | head -2
