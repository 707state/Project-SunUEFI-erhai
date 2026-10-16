#!/system/bin/sh
echo "=== entry15 first (offset 2048) / last (offset 2056) ==="
dd if=/dev/block/sda bs=1 skip=$((8192+15*128+32)) count=8 2>/dev/null | od -A n -t x1
dd if=/dev/block/sda bs=1 skip=$((8192+15*128+40)) count=8 2>/dev/null | od -A n -t x1
echo "expected first: b2 68 07 00 (121968434 LE)  last: e1 68 07 00... wait recompute"
echo "121968434 = 0x0745_68B2 -> LE: b2 68 45 07"
echo "122099505 = 0x0748_68E1 -> LE: e1 68 48 07"
echo "=== entry16 GUID (offset 2176) ==="
dd if=/dev/block/sda bs=1 skip=$((8192+16*128)) count=16 2>/dev/null | od -A n -t x1
echo "expected: af 3d c6 0f 83 84 72 47 8e 79 3d 69 d8 47 7d e4"
echo "=== entry16 first/last ==="
dd if=/dev/block/sda bs=1 skip=$((8192+16*128+32)) count=8 2>/dev/null | od -A n -t x1
dd if=/dev/block/sda bs=1 skip=$((8192+16*128+40)) count=8 2>/dev/null | od -A n -t x1
