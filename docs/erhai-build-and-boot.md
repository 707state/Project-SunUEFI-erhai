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
- Linux 内核未完成（erhai DTS 骨架在 `linux/dts/erhai/`，内核配置片段
  `linux/configs/erhai-first-boot.config`，构建流程见下节方向）
- ESP 里尚无内核/initramfs；BDS 45 秒自动重启是设计行为
- 刷 slot A 后 Android 侧 `/data` 缩小 ~8.5 GB；删除分区归还空间需要手动
  扩容（OxygenOS 的 fs_mgr 可能没有小米的 F2FS 自动扩容补丁）
- 内核源码树在 `upstream/linux-piano`（现已直接入库），erhai 板文件
  `arch/arm64/boot/dts/qcom/sm8750-oneplus-erhai.dts`

## 六、内核构建（进行中，备忘）

macOS 原生构建的已知障碍：内核 HOST 工具需要 glibc 头（`elf.h`、
`byteswap.h` 等），macOS 没有。可行路径：
1. podman 容器（`build/container/kernel-builder.Containerfile`，Debian +
   build-essential；podman VM 内存需 ≥4 GiB）
2. 或任意 Linux 机器/CI 上直接 `make ARCH=arm64` 用 gcc

内核 DTS 已能在 mac 上单独构建（`qcom/sm8750-oneplus-erhai.dtb`，
需 gmake + llvm）。配置：`piano_defconfig` +
`linux/configs/erhai-first-boot.config` 合并（EFI stub / UFS / GENI 串口 /
ext4 根 / USB）。

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
