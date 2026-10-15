#!/system/bin/sh
# erhai SunUEFI partition creator — FINAL with baked-in sector numbers.
# GPT uses 4096-byte LBAs on this UFS LUN (verified: sysfs sda15 start 28940688
# x512 == GPT first 3617586 x4096). Entries array at 4K-LBA16 (512B-LBA128).
#
# SAFETY: userdata START never moves; only END pulled in by 512MiB+8GiB.
#         F2FS shrink runs FIRST via the verified static tool; GPT is touched
#         only after the kernel confirms the new filesystem size.
#         Original GPT region is backed up before any write.
set -e
SDA=/dev/block/sda
HELPER=/data/local/tmp/erhai-resize-f2fs

# ---- baked-in plan (4096-byte GPT blocks) ----
USER_FIRST=3617586
USER_LAST_OLD=124196858
NEW_USER_LAST=121968433
ESP_FIRST=121968434
ESP_LAST=122099505
ROOT_FIRST=122099506
ROOT_LAST=124196858
NEW_USER_BYTES=484765073408

echo "=== plan ==="
echo "userdata: $USER_FIRST..$USER_LAST_OLD -> $USER_FIRST..$NEW_USER_LAST"
echo "sunuefi_esp : $ESP_FIRST..$ESP_LAST (512MiB, EFI system)"
echo "sunuefi_root: $ROOT_FIRST..$ROOT_LAST (8GiB, Linux fs)"
echo "F2FS target : $NEW_USER_BYTES bytes"
echo ""
echo "This shrinks /data ONLINE and rewrites 3 GPT entries."
echo "Backup of original entries goes to /data/local/tmp/erhai_gpt_orig.bin"
echo -n "Type YES to continue: "
read C
[ "$C" = "YES" ] || { echo aborted; exit 1; }

echo "=== 1. backup original GPT entries (4K LBAs 16..23) ==="
dd if=$SDA bs=4096 skip=16 count=8 of=/data/local/tmp/erhai_gpt_orig.bin
sync

echo "=== 2. F2FS online shrink (kernel ioctl on mounted /data) ==="
$HELPER shrink --target-bytes $NEW_USER_BYTES --execute || {
  echo "F2FS RESIZE FAILED — GPT untouched, /data intact. Aborting."; exit 1; }

echo "=== 3. patch GPT entries ==="
# Work on a copy of the entries area, patch 3 entries, write back.
dd if=$SDA bs=4096 skip=16 count=8 of=/data/local/tmp/gpt_work.bin

# write u64 LE decimal at byte offset (per-byte printf, LSB first)
put64() { # $1=file $2=offset $3=decimal
  OFF=$2; V=$3
  I=0
  while [ $I -lt 8 ]; do
    B=$(( (V / (1)) % 256 )); V=$(( V / 256 ))
    printf "$(awk -v b=$B 'BEGIN{printf "%02x", b}')" | dd of=$1 bs=1 seek=$((OFF+I)) count=1 conv=notrunc 2>/dev/null
    I=$((I+1))
  done
}
# write UTF-16LE name (max 36 chars incl NUL padding) at byte offset
putname() { # $1=file $2=offset $3=name
  OFF=$2; NAME=$3
  # pre-zero 72 bytes of name field
  dd if=/dev/zero of=$1 bs=1 seek=$OFF count=72 conv=notrunc 2>/dev/null
  I=0
  LEN=${#NAME}
  while [ $I -lt $LEN ]; do
    C=$(echo -n "$NAME" | cut -c$((I+1)))
    H=$(printf '%s' "$C" | od -A n -t x1 | tr -d ' ')
    printf "$H" | dd of=$1 bs=1 seek=$((OFF+I*2)) count=1 conv=notrunc 2>/dev/null
    I=$((I+1))
  done
}

W=/data/local/tmp/gpt_work.bin
# --- entry 14 (userdata): update last LBA + first/last usable stay ---
put64 $W $((14*128+40)) $NEW_USER_LAST

# --- entry 15 (sunuefi_esp): type GUID = EFI system partition C12A7328-F81F-11D2-BA4B-00A0C93EC93B ---
# write 16 type-guid bytes at entry15+0 (LE form: 28 73 2a c1 1f f8 d2 11 ba 4b 00 a0 c9 3e c9 3b)
printf '\x28\x73\x2a\xc1\x1f\xf8\xd2\x11\xba\x4b\x00\xa0\xc9\x3e\xc9\x3b' > /data/local/tmp/_g
dd if=/data/local/tmp/_g of=$W bs=1 seek=$((15*128)) count=16 conv=notrunc 2>/dev/null
put64 $W $((15*128+32)) $ESP_FIRST
put64 $W $((15*128+40)) $ESP_LAST
putname $W $((15*128+56)) "sunuefi_esp"

# --- entry 16 (sunuefi_root): type GUID = Linux filesystem 0FC63DAF-8483-4772-8E79-3D69D8477DE4 ---
printf '\xaf\x3d\xc6\x0f\x83\x84\x72\x47\x8e\x79\x3d\x69\xd8\x47\x7d\xe4' > /data/local/tmp/_g
dd if=/data/local/tmp/_g of=$W bs=1 seek=$((16*128)) count=16 conv=notrunc 2>/dev/null
put64 $W $((16*128+32)) $ROOT_FIRST
put64 $W $((16*128+40)) $ROOT_LAST
putname $W $((16*128+56)) "sunuefi_root"

echo "=== 4. write patched entries back ==="
dd if=$W of=$SDA bs=4096 seek=16 count=8 conv=notrunc
sync

echo "=== 5. verify readback ==="
dd if=$SDA bs=4096 skip=16 count=8 2>/dev/null > /data/local/tmp/gpt_check.bin
cmp -s $W /data/local/tmp/gpt_check.bin && echo "GPT entries verified OK" || echo "VERIFY FAILED — check manually!"

echo ""
echo "DONE. Now REBOOT Android (vold rebuilds the smaller userdata mapping)."
echo "After reboot: /data is 512MiB+8GiB smaller; new partitions appear after rescan."
echo "Rollback: dd if=/data/local/tmp/erhai_gpt_orig.bin of=$SDA bs=4096 seek=16 count=8"
