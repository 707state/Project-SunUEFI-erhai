#!/system/bin/sh
# Proper 512-byte comparison of the header at byte 4096:
dd if=/dev/block/sda bs=1 skip=4096 count=512 2>/dev/null > /data/local/tmp/hdr512.bin
cmp -s /data/local/tmp/erhai-gpt-header-patched.bin /data/local/tmp/hdr512.bin \
  && echo "header 512B verified OK" || echo "REAL MISMATCH"
echo "ArrayCrc field (offset 88):"
dd if=/dev/block/sda bs=1 skip=$((4096+88)) count=4 2>/dev/null | od -A n -t x1
echo "expected: c6 ab 85 06"
echo "HeaderCrc field (offset 16):"
dd if=/dev/block/sda bs=1 skip=$((4096+16)) count=4 2>/dev/null | od -A n -t x1
echo "expected: a0 52 8e 1f"
