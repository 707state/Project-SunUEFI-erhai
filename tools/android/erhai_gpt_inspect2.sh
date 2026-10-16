#!/system/bin/sh
# READ-ONLY erhai storage inspection v2 — no sgdisk dependency.
# GPT parsed with dd+od. Run: su -c 'sh /data/local/tmp/erhai_gpt_inspect2.sh'
set -e
SDA=/dev/block/sda
echo "=== sda size ==="
blockdev --getsize64 $SDA
SECTORS=$(( $(blockdev --getsize64 $SDA) / 512 ))
echo "sectors: $SECTORS"

echo "=== GPT header (LBA1) ==="
dd if=$SDA bs=512 skip=1 count=1 2>/dev/null | od -A d -t x1 | head -3

echo "=== partition entry count / bytes ==="
# GPT header: entries at offset 80 (u32), entry size at 84 (u16)
ENTRIES=$(dd if=$SDA bs=512 skip=1 count=1 2>/dev/null | od -A n -t u4 -j 80 -N 4 | tr -d ' ')
ENTSIZ=$(dd if=$SDA bs=512 skip=1 count=1 2>/dev/null | od -A n -t u2 -j 84 -N 2 | tr -d ' ')
echo "entries=$ENTRIES entry_size=$ENTSIZ"

echo "=== all partitions (dd-parse of entry array) ==="
# entries usually start at LBA2, 4 entries per sector (128B each)
N=0
for SEC in $(seq 2 $(( 2 + ENTRIES/4 - 1 )) ); do
  for OFF in 0 128 256 384; do
    N=$((N+1))
    [ $N -gt $ENTRIES ] && break 2
    # type GUID first 16 bytes: all-zero = unused entry
    TYPE=$(dd if=$SDA bs=512 skip=$SEC count=1 2>/dev/null | od -A n -t x1 -j $OFF -N 16 | tr -d ' \n')
    [ -z "$(echo $TYPE | tr -d '0')" ] && continue
    FIRST=$(dd if=$SDA bs=512 skip=$SEC count=1 2>/dev/null | od -A n -t u8 -j $((OFF+32)) -N 8 --endian=little | tr -d ' ')
    LAST=$(dd if=$SDA bs=512 skip=$SEC count=1 2>/dev/null | od -A n -t u8 -j $((OFF+40)) -N 8 --endian=little | tr -d ' ')
    NAME=$(dd if=$SDA bs=512 skip=$SEC count=1 2>/dev/null | dd bs=2 skip=$((OFF/2+36)) count=36 2>/dev/null | tr -d '\0' )
    echo "part$N: $NAME first=$FIRST last=$LAST size=$(( (LAST-FIRST+1)*512 ))"
  done
done

echo "=== userdata dm device search ==="
for d in /sys/class/block/dm-*; do
  NAME=$(cat "$d/dm/name" 2>/dev/null)
  case "$NAME" in
    userdata) DM=/dev/block/$(basename $d); echo "FOUND: $DM name=$NAME";;
  esac
done
[ -n "$DM" ] || echo "userdata dm NOT found by name — listing all dm names:"
[ -n "$DM" ] || for d in /sys/class/block/dm-*; do cat "$d/dm/name" 2>/dev/null; done | head -40

echo "=== mount points ==="
mount | grep -E "data|dm" | head -6

if [ -n "$DM" ]; then
  echo "=== dm sizes ==="
  echo "dm bytes: $(blockdev --getsize64 $DM)"
  echo "sda bytes: $(blockdev --getsize64 $SDA)"
  echo "=== dm table (key redacted — do not publish params!) ==="
  dmsetup table $DM 2>/dev/null | sed 's/ [0-9a-f]\{32,\}/ <REDACTED>/g' | head -3
  echo "=== F2FS magic via dm ==="
  dd if=$DM bs=4096 count=1 skip=0 2>/dev/null | od -A n -t x1 -N 4
fi
