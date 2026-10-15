#!/system/bin/sh
# Verify the pushed patched-bin content on the device:
echo "=== erhai-gpt-patched.bin on device (entry15 zone, offset 1920-1936) ==="
dd if=/data/local/tmp/erhai-gpt-patched.bin bs=1 skip=1920 count=16 2>/dev/null | od -A n -t x1
echo "=== expected: 28 73 2a c1 1f f8 d2 11 ba 4b 00 a0 c9 3e c9 3b ==="
echo "=== file size ==="
ls -la /data/local/tmp/erhai-gpt-patched.bin
echo "=== entry15 name zone (offset 1976): ==="
dd if=/data/local/tmp/erhai-gpt-patched.bin bs=1 skip=1976 count=24 2>/dev/null | od -A n -c | head -2
