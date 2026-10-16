#!/system/bin/sh
# READ-ONLY v3: fixes v2 bugs (32-bit shell arithmetic overflow; F2FS magic offset).
set -e
SDA=/dev/block/sda
echo "=== sizes ==="
SDA_BYTES=$(blockdev --getsize64 $SDA)
echo "sda bytes: $SDA_BYTES"
# use awk for 64-bit math
SDA_SECTORS=$(awk "BEGIN{printf \"%d\", $SDA_BYTES/512}")
echo "sda sectors: $SDA_SECTORS"
DM=/dev/block/dm-45
DM_BYTES=$(blockdev --getsize64 $DM)
echo "dm-45 bytes: $DM_BYTES  sectors: $(awk "BEGIN{printf \"%d\", $DM_BYTES/512}")"

echo "=== GPT header via dd conv=swab? plain LBA1 ==="
dd if=$SDA bs=512 skip=1 count=1 2>/dev/null | od -A n -t x1 -N 64
echo "=== LBA0 (protective MBR?) ==="
dd if=$SDA bs=512 skip=0 count=1 2>/dev/null | od -A n -t x1 -N 64

echo "=== find 'EFI PART' signature by scanning first 40 LBAs ==="
for L in $(seq 0 40); do
  SIG=$(dd if=$SDA bs=512 skip=$L count=1 2>/dev/null | dd bs=1 skip=512 count=8 2>/dev/null)
  true
done
# simpler: read signature directly
for L in 0 1 2 3 4 5 6 7 8; do
  S=$(dd if=$SDA bs=512 skip=$L count=1 2>/dev/null | head -c 8)
  echo "LBA$L first8: $S"
done

echo "=== partition entries: try LBA2, parse first 20 entries ==="
for IDX in $(seq 0 19); do
  SEC=$((2 + IDX/4))
  OFF=$(( (IDX%4)*128 ))
  # type guid nonzero check via first 4 bytes
  T=$(dd if=$SDA bs=512 skip=$SEC count=1 2>/dev/null | od -A n -t x1 -j $OFF -N 4 | tr -d ' ')
  Z=$(echo "$T" | tr -d '0 ')
  [ -z "$Z" ] && continue
  FIRST=$(dd if=$SDA bs=512 skip=$SEC count=1 2>/dev/null | od -A n -t u8 -j $((OFF+32)) -N 8 --endian=little | tr -d ' ')
  LAST=$(dd if=$SDA bs=512 skip=$SEC count=1 2>/dev/null | od -A n -t u8 -j $((OFF+40)) -N 8 --endian=little | tr -d ' ')
  NAMEHEX=$(dd if=$SDA bs=512 skip=$SEC count=1 2>/dev/null | od -A n -t x1 -j $((OFF+56)) -N 72 | tr -d ' \n')
  NAME=$(echo "$NAMEHEX" | awk '{s=""; for(i=1;i<=length($0);i+=4){c=strtonum("0x" substr($0,i+2,2)); if(c==0)break; s=s sprintf("%c",c)} print s}')
  echo "entry$IDX: name=$NAME first=$FIRST last=$LAST"
done

echo "=== F2FS superblock (offset 1024, via dm-45) ==="
dd if=$DM bs=1 skip=1024 count=4 2>/dev/null | od -A n -t x1
echo "=== F2FS superblock (offset 4096+1024, second copy) ==="
dd if=$DM bs=1 skip=5120 count=4 2>/dev/null | od -A n -t x1
echo "=== dm-45 table (redacted) ==="
dmsetup table dm-45 2>/dev/null | awk '{for(i=1;i<=NF;i++) if(length($i)>64) $i="<REDACTED>"; print}' | head -2
echo "=== /data mount ==="
mount | grep " /data "
