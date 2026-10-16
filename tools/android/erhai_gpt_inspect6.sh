#!/system/bin/sh
# READ-ONLY v6: hex->number without strtonum (works in toybox awk).
set -e
SDA=/dev/block/sda
dd if=$SDA bs=4096 skip=1 count=2 2>/dev/null > /data/local/tmp/gpt.bin
IDX=0
while [ $IDX -lt 32 ]; do
  OFF=$((IDX*128))
  T=$(dd if=/data/local/tmp/gpt.bin bs=1 skip=$OFF count=4 2>/dev/null | od -A n -t x1 | tr -d ' ')
  Z=$(echo "$T" | tr -d '0 ')
  [ -z "$Z" ] && { IDX=$((IDX+1)); continue; }
  F=$(dd if=/data/local/tmp/gpt.bin bs=1 skip=$((OFF+32)) count=8 2>/dev/null | od -A n -t x1 | tr -d ' \n')
  L=$(dd if=/data/local/tmp/gpt.bin bs=1 skip=$((OFF+40)) count=8 2>/dev/null | od -A n -t x1 | tr -d ' \n')
  FIRST=$(echo $F | awk 'BEGIN{h="0123456789abcdef"; v=0; n=split(ARGV[1],a," "); for(i=n;i>=1;i--){p=index(h,a[i])-1; if(p<0)p=0; v=v*256+p}} END{printf "%d", v}' "$F")
  LAST=$(echo $L | awk 'BEGIN{h="0123456789abcdef"; v=0; n=split(ARGV[1],a," "); for(i=n;i>=1;i--){p=index(h,a[i])-1; if(p<0)p=0; v=v*256+p}} END{printf "%d", v}' "$L")
  NAME=$(dd if=/data/local/tmp/gpt.bin bs=2 skip=$((OFF/2+28)) count=36 2>/dev/null | tr -d '\0')
  SZMB=$(awk -v f=$FIRST -v l=$LAST 'BEGIN{printf "%.0f", (l-f+1)*512/1048576}')
  echo "part$((IDX+1)): name=$NAME first=$FIRST last=$LAST size=${SZMB}MiB"
  IDX=$((IDX+1))
done
rm -f /data/local/tmp/gpt.bin
echo "=== sda15 (userdata backing) start/size ==="
echo "start: $(cat /sys/class/block/sda15/start)"
echo "size:  $(cat /sys/class/block/sda15/size)"
echo "=== dm-45 start/size ==="
echo "start: $(cat /sys/class/block/dm-45/start 2>/dev/null || echo n/a)"
echo "size:  $(cat /sys/class/block/dm-45/size 2>/dev/null || echo n/a)"
