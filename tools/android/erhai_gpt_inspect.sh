#!/system/bin/sh
# READ-ONLY erhai storage inspection. Writes nothing.
# Run as: su -c 'sh /data/local/tmp/erhai_gpt_inspect.sh'
BLOCK=$(find /dev/block/sda /dev/block/sde /dev/block/sdf 2>/dev/null | head -1)
[ -z "$BLOCK" ] && BLOCK=$(ls -d /dev/block/sd* 2>/dev/null | head -1)
echo "=== block device: $BLOCK ==="
ls -l /dev/block/by-name/ | grep -E "userdata|boot|misc"
echo "=== GPT partitions (sgdisk -p) ==="
sgdisk -p "$BLOCK" 2>/dev/null || echo "sgdisk unavailable"
echo "=== GPT detailed (sgdisk -i per partition) ==="
sgdisk -p "$BLOCK" 2>/dev/null | tail -n +7 | while read num rest; do
  sgdisk -i "$num" "$BLOCK" 2>/dev/null | grep -E "Partition|^First|^Last|Name|GUID" | head -6
  echo "---"
done
echo "=== userdata dm mapping ==="
ls -l /dev/block/mapper/ 2>/dev/null | head
for d in /dev/block/mapper/*; do
  NAME=$(cat /sys/class/block/$(basename "$d")/dm/name 2>/dev/null)
  [ "$NAME" = "userdata" ] && echo "userdata mapper: $d"
done
echo "=== F2FS superblock sanity (via dm device, read-only) ==="
USERDM=$(for d in /dev/block/mapper/*; do
  NAME=$(cat /sys/class/block/$(basename "$d")/dm/name 2>/dev/null)
  [ "$NAME" = "userdata" ] && echo "$d"
done)
if [ -n "$USERDM" ]; then
  dd if="$USERDM" bs=4096 skip=0 count=16 2>/dev/null | od -A d -t x1 | head -4
  blockdev --getsize64 "$USERDM" 2>/dev/null
  blockdev --getsize64 "$BLOCK" 2>/dev/null
fi
