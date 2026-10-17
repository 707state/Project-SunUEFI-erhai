# erhai 构建与刷入指南

本页说明如何从本仓库构建 SunUEFI erhai 固件镜像，以及构建完成后如何在
OnePlus Pad 2 Pro（`erhai`）上运行。所有上游依赖已并入仓库（无子模块）。

## 前置条件（macOS / ARM64 Mac）

| 工具 | 安装 | 用途 |
| --- | --- | --- |
| llvm（brew `llvm`） | `brew install llvm` | UEFI 与内核交叉编译 |
| musl-cross | `brew install filosottile/musl-cross/musl-cross` | 静态 ARM64 用户态工具 |
| make（GNU） | `brew install make`（用 `gmake`） | 内核构建（3.81 不够） |
| acpica + nasm | `brew install acpica nasm` | UEFI ACPI/汇编 |
| python3 + venv | 系统自带 | 构建脚本 |
| dtc（可选） | 仓库自带 `upstream/dtc/dtc` | 设备树工具 |

首次准备：

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements-build.txt
```

工具链 shim（把 brew 的 llvm 两个 formula 拼成 EDK2 需要的布局）：

```sh
mkdir -p build/host-tools-macos/usr/bin
cd build/host-tools-macos/usr/bin
for t in clang llvm-lib llvm-objcopy llvm-nm; do ln -sf /opt/homebrew/opt/llvm/bin/$t $t; done
ln -sf /opt/homebrew/bin/lld-link lld-link
ln -sf /opt/homebrew/bin/iasl iasl
ln -sf /opt/homebrew/bin/nasm nasm
ln -sf /opt/homebrew/opt/llvm/bin/llvm-objcopy aarch64-linux-gnu-objcopy
cd ../../..
```

## 一、构建 UEFI 固件镜像

```sh
# 1. 刷新固件工作区（把 erhaiPkg / vendor 提取件同步进构建副本）
python3 tools/firmware_workspace.py prepare

# 2. 编译（DEBUG 诊断构建；纯 RELEASE 把 DEBUG 换成 RELEASE）
SUNUEFI_BUILD_TARGET=DEBUG \
SUNUEFI_TOOLCHAIN_ROOT="$PWD/build/host-tools-macos/usr" \
bash build/firmware-workspace/tools/build_stage0.sh erhai

# 3. 打包 fastboot-boot 独立镜像
python3 tools/package_erhai_stage0.py
```

产物：
- `artifacts/erhai/ErhaiUEFI-stage0.img` — 独立镜像（内核=gzip(BootShim+FD)+DTB，
  ramdisk 为空），用 `fastboot boot` 临时运行，不写任何分区
- `artifacts/erhai/erhai-stage0.fd` + `BootShim.bin` — 固件本体（合体 BOOT 用）

调试提示：修改 `uefi/platforms/erhaiPkg/`、`vendor/erhai/`、`tools/` 后必须重跑
步骤 1（工作区陈旧会被 `build_integrity` 拦截：`Firmware workspace source is
stale`）。怀疑构建缓存时，在步骤 2 前加
`rm -rf build/firmware-workspace/upstream/Mu-Silicium/Build/erhaiPkg`。

## 二、构建合体 BOOT 镜像（slot A 常驻）

输入是当前活动槽（slot B）的原厂 boot 镜像，从设备提取：

```sh
adb shell su -c 'dd if=/dev/block/by-name/boot_b of=/sdcard/boot_b.img'
adb pull /sdcard/boot_b.img .
```

打包（96 MiB 安装容器，AVB unsigned 保留原指纹）：

```sh
SUNUEFI_TOOLCHAIN_ROOT="$PWD/build/host-tools-macos/usr" \
python3 tools/package_erhai_trampoline.py \
  --stock-boot boot_b.img \
  --output build/erhai-combined
```

## 三、设备分区（一次性）

`tools/android/` 下有完整工具链。分区已在位的设备跳过本节。

1. **只读勘察**：`erhai_gpt_inspect.sh`（无 sgdisk 依赖版本见 inspect6/7）
2. **F2FS 在线缩容**：静态工具 `build/erhai-resize-f2fs`（musl-cross 编译）
3. **GPT 切分**：`erhai_gpt_partition_final.sh`（内置扇区数）+
   `build/erhai-gpt-patched.bin`（条目补丁块）+ `erhai_gpt_fix_v2.sh`（写入）+
   `build/erhai-gpt-header-patched.bin` + `erhai_gpt_crc_fix.sh`（双 CRC 修复）

分区结果（4K GPT 块）：userdata 3617586..121968433（缩小后）、
`sunuefi_esp` 121968434..122099505（512 MiB）、`sunueui_root`
122099506..124196858（8 GiB）。保护性 MBR 见 `erhai_write_pmbr.sh`。

ESP 格式化（主机构建 FAT32 镜像后 dd）：

```sh
adb push build/sunuefi_esp.img /data/local/tmp/
adb shell su -c 'dd if=/data/local/tmp/sunuefi_esp.img of=/dev/block/sda16 bs=4096 conv=notrunc && sync'
```

## 四、刷入并运行

### 临时运行（不改分区，最快验证）

```sh
adb reboot bootloader
fastboot boot artifacts/erhai/ErhaiUEFI-stage0.img
```

屏幕出现 `SunUEFI: OnePlus Pad 2 Pro / erhai / SM8750-AB` 横幅、
`Storage summary: 6 block devices, N filesystems` 后约 45 秒自动重启回
Android（设计行为；stage-0 没有可启动 OS）。启动较慢（每条 UFS 读都有
doorbell 轮询）属正常。

如果 ESP 里已经有内核（`\EFI\BOOT\BOOTAA64.EFI`），BDS 在没有 `Boot####`
时会把 PlatformRecovery 默认项展开到该文件并直接启动，45 秒自动重启不再
触发。内核、DTB 和 initramfs 的放置方式见第六节。

### 常驻 slot A（日常使用）

```sh
adb reboot bootloader
fastboot flash boot_a build/erhai-combined/install-container.bin
fastboot set_active a
fastboot reboot
```

开机出现 3 秒选择页（音量键切换 Android/UEFI，电源键确认）；选 UEFI 进入
固件；选 Android 直接进 slot A 的原厂 Android（注意是上一 OTA 槽的系统）。
回滚：`fastboot set_active b && fastboot reboot`——slot B 全套分区原封未动。

### 关键实机日志（判读）

| 日志 | 含义 |
| --- | --- |
| `SUNUEFI_UFS_BLOCKIO_PUBLISHED parents=6` | 6 个 LUN 的只读 BlockIO 发布 |
| `SUNUEFI_UFS_GPT_VERIFIED ... active_partitions=17` | 补丁后 GPT 解析成功 |
| `FileSystem N opened` / `filesystems>=1` | FAT 挂上 ESP（第二层闭环） |
| `ERHAI_UFS_RESULT Success` | UFS 引擎完整跑通 |

## 五、当前边界（诚实清单）

- BlockIO 只读（写一律 `EFI_WRITE_PROTECTED`）；有界写窗口未移植
- 内核已用 LLVM 在 macOS 上编出（`Image` 与
  `sm8750-oneplus-erhai.dtb`，见第六节），尚未实机启动验证
- ESP 里尚无内核/initramfs；放入后 BDS 会走 PlatformRecovery 启动。
  45 秒自动重启只在没有可启动镜像时触发
- erhai DTS 仍是 first-boot 骨架：只有 UART7、UFS、USB，没有显示、触摸、
  电源、音频、WLAN/BT
- 刷 slot A 后 Android 侧 `/data` 缩小 ~8.5 GB；删除分区归还空间需要手动
  扩容（OxygenOS 的 fs_mgr 可能没有小米的 F2FS 自动扩容补丁）
- 内核源码树在 `upstream/linux-piano`（现已直接入库），erhai 板文件
  `arch/arm64/boot/dts/qcom/sm8750-oneplus-erhai.dts`

## 六、内核构建（macOS 原生 LLVM，已编译通过）

本节记录在当前 macOS/ARM64 工作站上的实际构建路径。产物是主机编译结果，
尚未实机启动验证。

### 配置要点

`linux/configs/erhai-first-boot.config` 合并到 `piano_defconfig` 之上：

- `console=ttyMSM0`。GENI 驱动注册的控制台名是 `ttyMSM`，不是 `ttyMS0`。
- `CONFIG_CMDLINE_FROM_BOOTLOADER=y`，并显式取消 `CONFIG_CMDLINE_FORCE`。
  `piano_defconfig` 自带 `CMDLINE_FORCE=y`，不清掉会覆盖 erhai 的命令行。
- 命令行里的 `dtb=EFI/BOOT/erhai.dtb` 和 `initrd=EFI/BOOT/initramfs.cpio.gz`
  是相对 FAT 卷根的路径；EFI stub 从内核所在的 ESP 读取它们。
- `root=LABEL=sunuefi_root`。GPT 脚本没有写分区唯一 GUID，`PARTUUID=` 不可用。
- UFS 依赖全部内建：`SCSI_UFS_QCOM`、`PHY_QCOM_QMP_UFS`、`PINCTRL_SM8750`。
  这些在 `piano_defconfig` 里是模块，不配 initramfs 模块时根分区挂不上。

### 主机依赖与兼容层

```sh
brew install llvm make gnu-sed openssl@3
```

macOS 原生构建需要三个只存在于 `build/` 的兼容层（`build/` 不入库）：

| 文件 | 用途 |
| --- | --- |
| `build/host-include-macos/elf.h` | glibc 的 `elf.h` 拷贝，`scripts/mod` 需要 |
| `build/host-include-macos/byteswap.h` | 用 `__builtin_bswap{16,32,64}` 实现 |
| `build/host-include-macos/host-macos-compat.h` | `O_LARGEFILE=0` 与 `copy_file_range` 的 pread/pwrite 回退，供 `usr/gen_init_cpio` |

另外，Darwin 的 `<sys/types.h>` 已经定义了 `uuid_t`，会与
`scripts/mod/file2alias.c` 的局部 typedef 冲突。仓库里的补丁
`patches/linux/macos-host-tools.patch` 在 `__APPLE__` 下换用
`linux_uuid_t`。在干净源码树上先应用：

```sh
patch -p1 -d upstream/linux-piano < patches/linux/macos-host-tools.patch
```

### 构建命令

```sh
export PATH=/opt/homebrew/opt/llvm/bin:/opt/homebrew/opt/make/libexec/gnubin:/opt/homebrew/opt/gnu-sed/libexec/gnubin:$PATH
O=$PWD/build/kernels/erhai-first
K="gmake -C upstream/linux-piano O=$O ARCH=arm64 LLVM=1 LLVM_IAS=1 CROSS_COMPILE=aarch64-linux-gnu-"

$K piano_defconfig

# merge_config.sh 在 macOS 上需要 GNU readlink -m；没有 coreutils 时直接追加，
# kconfig 以最后出现的值为准。
cat $O/.config linux/configs/erhai-first-boot.config > $O/.config.merged
mv $O/.config.merged $O/.config
$K olddefconfig

HOSTCFLAGS="-I$PWD/build/host-include-macos -I/opt/homebrew/opt/openssl@3/include -include $PWD/build/host-include-macos/host-macos-compat.h" \
HOSTLDFLAGS="-L/opt/homebrew/opt/openssl@3/lib -Wl,-rpath,/opt/homebrew/opt/openssl@3/lib" \
  $K -j"$(sysctl -n hw.ncpu)" Image dtbs
```
erhai Linux构建命令
```
export PATH=/opt/homebrew/opt/llvm/bin:/opt/homebrew/opt/make/libexec/gnubin:/opt/homebrew/opt/gnu-sed/libexec/gnubin:$PATH
O=$PWD/build/kernels/erhai-first
HOSTCFLAGS="-I$PWD/build/host-include-macos -I/opt/homebrew/opt/openssl@3/include -include
$PWD/build/host-include-macos/host-macos-compat.h" \
HOSTLDFLAGS="-L/opt/homebrew/opt/openssl@3/lib -Wl,-rpath,/opt/homebrew/opt/openssl@3/lib" \
  gmake -C upstream/linux-piano O=$O ARCH=arm64 LLVM=1 LLVM_IAS=1 \
        CROSS_COMPILE=aarch64-linux-gnu- -j12 Image dtbs
```
说明：`CONFIG_SYSTEM_TRUSTED_KEYRING=y` 会构建 `certs/extract-cert`，所以需要
OpenSSL 头文件和库路径；`-include` 的兼容头只作用于 host 工具。

### 产物与校验

| 产物 | 实际结果 |
| --- | --- |
| `build/kernels/erhai-first/arch/arm64/boot/Image` | 约 50 MiB，ARM64 + PE32+ EFI stub |
| `build/kernels/erhai-first/arch/arm64/boot/dts/qcom/sm8750-oneplus-erhai.dtb` | 约 114 KiB，`oneplus,erhai` / `qcom,sm8750` |

`tools/build_kernel.py` 的 `image_info()` 对 `Image` 的检查结果是
`arm64_magic=true`、`efi_stub=true`。内核 release 为 `7.2.6`（piano 基线，
不是 `7.2.9` 发布 pin）。

### Linux 容器回退

没有 macOS 兼容层时，用完整构建镜像 `containers/Dockerfile`
（含 `gcc-aarch64-linux-gnu`、`mtools`、`e2fsprogs`）：

```sh
docker build -t sunuefi-builder -f containers/Dockerfile .
docker run --rm -it --privileged -v "$PWD:/workspace" -w /workspace sunuefi-builder bash
```

容器里把上面的 `LLVM=1 LLVM_IAS=1` 换成 `CROSS_COMPILE=aarch64-linux-gnu-`，
并去掉 `HOSTCFLAGS`/`HOSTLDFLAGS` 和 macOS 补丁。`dtbs` 在 Linux 上不需要
额外的 `sed` 或 `readlink` 兼容处理。

## 附：仓库布局速览

```
uefi/platforms/erhaiPkg/     erhai 平台包（DSC/FDF/驱动）
uefi/core/                   piano 移植的 UFS DMA 引擎（ErhaiUfsDxe 内编译）
vendor/erhai/                本机固件提取（native/、cfg/、slt/）与采集件（captures/）
tools/android/               Android 侧分区/缩容/勘察脚本
upstream/                    直接入库的上游树（Mu-Silicium、linux-piano 等）
build/                       工作目录（host-tools-macos shim、内核输出等）
artifacts/erhai/             固件产物
```
