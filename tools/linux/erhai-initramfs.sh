#!/bin/bash
# Build the minimal erhai initramfs (static musl init + cpio.gz).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
D="$ROOT/build/initramfs"
mkdir -p "$D"/{bin,proc,sys,dev/pts,etc,root,tmp}
cat > "$D/init.c" <<'CEOF'
#include <sys/mount.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
int main(void){
  mount("none","/proc","proc",0,0);
  mount("none","/sys","sysfs",0,0);
  mount("none","/dev","devtmpfs",0,0);
  mkdir("/dev/pts",0755);
  mount("none","/dev/pts","devpts",0,0);
  int fd=open("/dev/console",O_RDWR);
  if(fd>=0){dup2(fd,0);dup2(fd,1);dup2(fd,2);}
  printf("\r\n=== erhai Linux first-boot ===\r\n");
  printf("kernel 7.2.6-piano EFI stub / DTB oneplus,erhai\r\n");
  printf("minimal static init (musl); busybox shell pending\r\n\r\n");
  fflush(stdout);
  for(int i=0;;i++){printf("[erhai-init] alive %d\r\n",i);fflush(stdout);sleep(10);}
  return 0;
}
CEOF
/opt/homebrew/bin/aarch64-linux-musl-gcc -static -Os -o "$D/init" "$D/init.c"
/opt/homebrew/bin/aarch64-linux-musl-strip "$D/init"
chmod 755 "$D/init"
cd "$D" && find . -not -name init.c | cpio -o -H newc --owner 0:0 2>/dev/null \
  | gzip -9 > "$ROOT/build/erhai-initramfs.cpio.gz"
echo "initramfs: $ROOT/build/erhai-initramfs.cpio.gz"
