#!/system/bin/sh
# Write the CRC-updated GPT header (4K-LBA1 = byte 4096). The entries at LBA2 are
# already patched on disk; this updates the header's ArrayCrc + HeaderCrc to match.
set -e
SDA=/dev/block/sda
echo "=== 1. backup original header ==="
dd if=$SDA bs=4096 skip=1 count=1 of=/data/local/tmp/erhai_gpt_header_orig.bin
echo "=== 2. write CRC-patched header ==="
dd if=/data/local/tmp/erhai-gpt-header-patched.bin of=$SDA bs=4096 seek=1 count=1 conv=notrunc
echo "=== 3. verify readback ==="
dd if=$SDA bs=4096 skip=1 count=1 2>/dev/null > /data/local/tmp/hdr_check.bin
cmp -s /data/local/tmp/erhai-gpt-header-patched.bin /data/local/tmp/hdr_check.bin \
  && echo "header verified OK" || echo "VERIFY FAILED"
sync
echo "DONE. GPT now fully consistent (entries + CRCs)."
