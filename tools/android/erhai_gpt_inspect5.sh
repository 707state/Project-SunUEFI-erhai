#!/system/bin/sh
# READ-ONLY v5: entries at LBA2 (confirmed). Manual LE decode without od --endian.
set -e
SDA=/dev/block/sda
echo "=== 32 entries from LBA2 (8 sectors) ==="
dd if=$SDA bs=4096 skip=1 count=2 2>/dev/null > /data/local/tmp/gpt_entries.bin
IDX=0
while [ $IDX -lt 32 ]; do
  OFF=$((IDX*128))
  T=$(dd if=/data/local/tmp/gpt_entries.bin bs=1 skip=$OFF count=4 2>/dev/null | od -A n -t x1 | tr -d ' ')
  Z=$(echo "$T" | tr -d '0 ')
  [ -z "$Z" ] && { IDX=$((IDX+1)); continue; }
  # first LBA at entry+32, little-endian u64: read bytes and assemble with awk
  F=$(dd if=/data/local/tmp/gpt_entries.bin bs=1 skip=$((OFF+32)) count=8 2>/dev/null | od -A n -t x1 | tr -d ' \n')
  L=$(dd if=/data/local/tmp/gpt_entries.bin bs=1 skip=$((OFF+40)) count=8 2>/dev/null | od -A n -t x1 | tr -d ' \n')
  FIRST=$(echo $F | awk '{v=0; for(i=NF;i>=1;i--) v=v*256+strtonum("0x"$i); printf "%d", v}')
  LAST=$(echo $L | awk '{v=0; for(i=NF;i>=1;i--) v=v*256+strtonum("0x"$i); printf "%d", v}')
  NAME=$(dd if=/data/local/tmp/gpt_entries.bin bs=2 skip=$((OFF/2+28)) count=36 2>/dev/null | tr -d '\0')
  SZMB=$(awk "BEGIN{printf \"%.0f\", ($LAST-$FIRST+1)*512/1048576}")
  echo "part$((IDX+1)): name=$NAME first=$FIRST last=$LAST size=${SZMB}MiB"
  IDX=$((IDX+1))
done
rm -f /data/local/tmp/gpt_entries.bin
echo "=== dm-45 start/size ==="
cat /sys/class/block/dm-45/start 2>/dev/null || echo "no start attr"
cat /sys/class/block/dm-45/size 2>/dev/null || echo "no size attr"
echo "=== sda15 start/size ==="
cat /sys/class/block/sda15/start 2>/dev/null
cat /sys/class/block/sda15/size 2>/dev/null
