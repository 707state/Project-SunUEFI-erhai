#!/system/bin/sh
# READ-ONLY v4: GPT header found at LBA8 — parse entries from there.
set -e
SDA=/dev/block/sda

echo "=== GPT header at LBA8 (first 96 bytes) ==="
dd if=$SDA bs=512 skip=8 count=1 2>/dev/null | od -A d -t x1 -N 96

echo "=== header fields ==="
HDR=$(dd if=$SDA bs=512 skip=8 count=1 2>/dev/null | od -A n -t u4 -j 80 -N 4 | tr -d ' ')
HSZ=$(dd if=$SDA bs=512 skip=8 count=1 2>/dev/null | od -A n -t u2 -j 84 -N 2 | tr -d ' ')
ENTS_LBA=$(dd if=$SDA bs=512 skip=8 count=1 2>/dev/null | od -A n -t u8 -j 72 -N 8 --endian=little | tr -d ' ')
ENTRIES=$(dd if=$SDA bs=512 skip=8 count=1 2>/dev/null | od -A n -t u4 -j 80 -N 4 | tr -d ' ')
echo "entries_lba=$ENTS_LBA entries=$ENTRIES entry_size=$HSZ"

echo "=== backup GPT header: last sectors ==="
SDA_SECTORS=$(awk "BEGIN{printf \"%d\", $(blockdev --getsize64 $SDA)/512}")
echo "sda sectors: $SDA_SECTORS"
dd if=$SDA bs=512 skip=$((SDA_SECTORS-1)) count=1 2>/dev/null | od -A n -t x1 -N 16

echo "=== partition entries (parse from entries_lba) ==="
ESEC=$ENTS_LBA
IDX=0
MAX=$((ENTRIES))
while [ $IDX -lt $MAX ] && [ $IDX -lt 40 ]; do
  SEC=$((ESEC + IDX/4))
  OFF=$(( (IDX%4)*128 ))
  T=$(dd if=$SDA bs=512 skip=$SEC count=1 2>/dev/null | od -A n -t x1 -j $OFF -N 4 | tr -d ' ')
  Z=$(echo "$T" | tr -d '0 ')
  [ -z "$Z" ] && { IDX=$((IDX+1)); continue; }
  FIRST=$(dd if=$SDA bs=512 skip=$SEC count=1 2>/dev/null | od -A n -t u8 -j $((OFF+32)) -N 8 --endian=little | tr -d ' ')
  LAST=$(dd if=$SDA bs=512 skip=$SEC count=1 2>/dev/null | od -A n -t u8 -j $((OFF+40)) -N 8 --endian=little | tr -d ' ')
  NAME=$(dd if=$SDA bs=512 skip=$SEC count=1 2>/dev/null | dd bs=2 skip=$((OFF/2+28)) count=36 2>/dev/null | tr -d '\0')
  SZMB=$(awk "BEGIN{printf \"%.0f\", ($LAST-$FIRST+1)*512/1048576}")
  echo "part$((IDX+1)): name=$NAME first=$FIRST last=$LAST size=${SZMB}MiB"
  IDX=$((IDX+1))
done

echo "=== userdata dm-45 backing (sysfs slaves) ==="
ls -l /sys/class/block/dm-45/slaves/ 2>/dev/null
echo "=== dm-45 start offset from table via /sysfs ==="
cat /sys/class/block/dm-45/start 2>/dev/null
cat /sys/class/block/dm-45/size 2>/dev/null
