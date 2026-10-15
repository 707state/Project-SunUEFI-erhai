#!/system/bin/sh
# erhai SunUEFI partition creator: shrink /data online, then write sunuefi_esp +
# sunuefi_root GPT entries in the freed tail. APatch/root required.
#
# SAFETY: - GPT is written only after F2FS resize reports success
#         - userdata START is never moved, only its END is pulled in
#         - a backup of the original GPT header+entries is saved to /data/local/tmp
#         - nothing else on the disk is touched
#
# Usage: su -c 'sh /data/local/tmp/erhai_gpt_partition.sh <ESP_MIB> <ROOT_GIB>'
#   e.g. su -c 'sh /data/local/tmp/erhai_gpt_partition.sh 512 8'

ESP_MIB=${1:-512}
ROOT_GIB=${2:-8}

BLOCK=""
for d in /dev/block/sde /dev/block/sda /dev/block/sdf /dev/block/sdg; do
  [ -b "$d" ] && BLOCK="$d" && break
done
[ -z "$BLOCK" ] && { echo "ERROR: no /dev/block/sd* found"; exit 1; }
echo "block device: $BLOCK"

USERDM=""
for d in /dev/block/mapper/*; do
  NAME=$(cat /sys/class/block/$(basename "$d")/dm/name 2>/dev/null)
  [ "$NAME" = "userdata" ] && USERDM="$d"
done
[ -z "$USERDM" ] && { echo "ERROR: userdata dm not found"; exit 1; }
echo "userdata mapper: $USERDM"

DISK_BYTES=$(blockdev --getsize64 "$BLOCK")
DM_BYTES=$(blockdev --getsize64 "$USERDM")
echo "disk bytes: $DISK_BYTES  userdata dm bytes: $DM_BYTES"
[ "$DM_BYTES" -ge "$DISK_BYTES" ] && { echo "ERROR: dm larger than disk?"; exit 1; }

SECTOR=512
DM_SECTORS=$((DM_BYTES / SECTOR))
ESP_SECTORS=$((ESP_MIB * 1024 * 1024 / SECTOR))
ROOT_SECTORS=$((ROOT_GIB * 1024 * 1024 * 1024 / SECTOR))

# Read userdata current start/end from GPT (sgdisk output)
# sgdisk -i N gives: First sector / Last sector / partition name
USERINFO=$(sgdisk -p "$BLOCK" | grep userdata)
USERNUM=$(echo "$USERINFO" | awk '{print $1}')
sgdisk -i "$USERNUM" "$BLOCK" | grep -E "First sector|Last sector|Partition unique GUID"
FIRST=$(sgdisk -i "$USERNUM" "$BLOCK" | grep "First sector" | grep -o '[0-9]*')
LAST=$(sgdisk -i "$USERNUM" "$BLOCK" | grep "Last sector" | grep -o '[0-9]*')
echo "userdata partition $USERNUM: sectors $FIRST..$LAST (current)"
USER_GUID=$(sgdisk -i "$USERNUM" "$BLOCK" | grep "unique GUID" | awk '{print $3}')

NEW_LAST=$((FIRST + DM_SECTORS - 1))
NEW_USER_SECTORS=$((NEW_LAST - FIRST + 1))
echo "userdata will shrink to sectors $FIRST..$NEW_LAST"

ESP_FIRST=$((NEW_LAST + 1))
ESP_LAST=$((ESP_FIRST + ESP_SECTORS - 1))
ROOT_FIRST=$((ESP_LAST + 1))
ROOT_LAST=$((ROOT_FIRST + ROOT_SECTORS - 1))
TAIL_END=$((DISK_BYTES / SECTOR - 34 - 1))
echo "plan: esp $ESP_FIRST..$ESP_LAST, root $ROOT_FIRST..$ROOT_LAST (tail end $TAIL_END)"
[ "$ROOT_LAST" -gt "$TAIL_END" ] && { echo "ERROR: not enough tail space"; exit 1; }
# ensure we are carving from the free tail: old userdata end must cover ROOT_LAST
[ "$ROOT_LAST" -gt "$LAST" ] && { echo "ERROR: plan exceeds old userdata end"; exit 1; }

echo "This will: 1) online-shrink F2FS to $((NEW_USER_SECTORS * SECTOR / 1024 / 1024)) MiB"
echo "           2) rewrite GPT: userdata end, + sunuefi_esp ($ESP_MIB MiB), + sunuefi_root ($ROOT_GIB GiB)"
echo "GPT backup will be saved to /data/local/tmp/erhai_gpt_backup.bin"
echo -n "Type YES to continue: "
read CONFIRM
[ "$CONFIRM" = "YES" ] || { echo "aborted"; exit 1; }

# 1) backup GPT (34 sectors head + 33 tail)
dd if="$BLOCK" of=/data/local/tmp/erhai_gpt_backup.bin bs=$SECTOR count=34 2>/dev/null
dd if="$BLOCK" of=/data/local/tmp/erhai_gpt_backup_tail.bin bs=$SECTOR skip=$((DISK_BYTES / SECTOR - 33)) count=33 2>/dev/null
sync

# 2) online F2FS shrink through the dm device (kernel ioctl via resize.f2fs or ioctl)
# Use the statically compiled helper (built from piano_resize_f2fs.c, musl static):
# it verifies the dm mapping is a full-length zero-offset userdata mapping, checks
# free blocks, then issues F2FS_IOC_RESIZE_FS on the MOUNTED /data and reads back
# both superblocks. Target bytes = new userdata sector count * 512.
NEW_USER_BYTES=$((NEW_USER_SECTORS * 512))
/data/local/tmp/erhai-resize-f2fs shrink --target-bytes "$NEW_USER_BYTES" --execute || {
  echo "ERROR: F2FS resize failed — GPT NOT touched, /data intact"; exit 1; }
echo "F2FS shrunk OK"

# 3) rewrite GPT: delete userdata, recreate with same GUID + new end, add esp/root
sgdisk --delete "$USERNUM" "$BLOCK"
sgdisk --new="$USERNUM:$FIRST:$NEW_LAST" --change-name="$USERNUM:userdata" \
  --partition-guid="$USERNUM:$USER_GUID" "$BLOCK"
sgdisk --new=":$ESP_FIRST:$ESP_LAST" --change-name=":sunuefi_esp" \
  --typecode=:EF00 "$BLOCK"
sgdisk --new=":$ROOT_FIRST:$ROOT_LAST" --change-name=":sunuefi_root" \
  --typecode=:8300 "$BLOCK"
sgdisk -p "$BLOCK"
sync
echo "DONE. Reboot Android now; vold will rebuild the dm mapping for the smaller userdata."
echo "Then: fastboot boot the UEFI image — BlockIO should enumerate sunuefi_esp."
