#!/system/bin/sh
# erhai GPT fix v2: writes the HOST-PREBUILT patched entries block to the REAL
# location (4K-LBA2 = byte 8192) and zeros the wrongly-patched area (4K-LBA16..23).
# Prerequisites: F2FS already shrunk (done); /data mounted OK.
# The patched block was built on the Mac from the pristine pull — verified entry
# values: userdata last=121968433, esp 121968434..122099505, root 122099506..124196858.
set -e
SDA=/dev/block/sda
P=$PWD
echo "=== 1. backup REAL entries (4K-LBA2, 4096 bytes) ==="
dd if=$SDA bs=4096 skip=2 count=1 of=/data/local/tmp/erhai_gpt_real_orig.bin
echo "=== 2. write host-prebuilt patched block ==="
dd if=/data/local/tmp/erhai-gpt-patched.bin of=$SDA bs=4096 seek=2 count=1 conv=notrunc
echo "=== 3. zero the wrongly-patched area (4K-LBA16..23) ==="
dd if=/dev/zero of=$SDA bs=4096 seek=16 count=8 conv=notrunc
echo "=== 4. verify readback ==="
dd if=$SDA bs=4096 skip=2 count=1 2>/dev/null > /data/local/tmp/gpt_check.bin
cmp -s /data/local/tmp/erhai-gpt-patched.bin /data/local/tmp/gpt_check.bin \
  && echo "REAL GPT entries verified OK" || echo "VERIFY FAILED"
sync
echo "DONE. Reboot Android; then check /dev/block/sda16 sda17 exist."
echo "Rollback: dd if=/data/local/tmp/erhai_gpt_real_orig.bin of=$SDA bs=4096 seek=2 count=1"
