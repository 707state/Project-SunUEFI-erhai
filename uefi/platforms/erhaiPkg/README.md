# erhaiPkg：OnePlus Pad 2 Pro / SM8750-AB stage-0 平台快照

`erhai`（OnePlus Pad 2 Pro，型号 OPD2415 / OP6190L1，骁龙 8 至尊版 SM8750-AB，13.2" 3.4K 屏）
的 stage-0 诊断平台包。与 `pianoPkg` 同一构建约定：tracked 快照、stage-0 范围
（无持久变量、存储、capsule、OS 启动）、显示继承（SimpleFbDxe 直接使用 ABL
点亮的 framebuffer）。

## 当前来源与边界

本包**没有 erhai 实机采集**。所有板级输入来自两个可核查的替代来源：

| 内容 | 来源 | 说明 |
| --- | --- | --- |
| 原生 DXE 二进制 | 固定上游 Mu-Silicium `Binaries/dodge`（OnePlus 13，SM8750-AB） | 同 SoC 同厂商的官方固件提取，上游固定 commit；包括 EnvDxeEnhanced、ChipInfoDxe、PlatformInfoDxe、DALSYSDxe、HWIODxe、HALIOMMUDxe、SmemDxe。原生驱动派发顺序必须遵循 dodge 自己的 `Include/APRIORI.inc`（SmemDxe → DALSYSDxe → HWODxe → ChipInfo → PlatformInfo → HALIOMMU）：ChipInfoDxe 内静态链接了 DALModEnv，若在 DALSys/Smem 之前派发会在 `DALModEnv.c+115 !EFI_ERROR` 卡死。**不含 TZ 三件套**（ShmBridgeDxeLA/ScmDxeCompat/TzDxeLA）：实机 fastboot boot 显示 dodge 的 `BOOT.MXF.2.5.1` TzDxeLA 在 erhai 的 TZ 固件上 `SmcInvokeInit` 返回 `EFI_NOT_FOUND(0xE)`、`ActivateTzEmbeddedSecureImage` 失败并卡死 DXE 派发；stage-0 无 SCM 依赖。恢复 TZ 需从 erhai 本机固件提取其原生 DXE（走 `vendor/piano` 同款流程），不能用 OnePlus 13 的 |
| 内存映射 / 配置映射 | 固定上游 `Platforms/OnePlus/dodgePkg` 的 MemoryMapLib / ConfigurationMapLib | SM8750-AB SoC 级布局（UEFI_FD 0xA7100000/3 MiB、栈 0xA760D000 等），与 piano 采集件一致 |
| 设备身份 | LineageOS `android_device_oneplus_erhai`（OPD2415、project-id 24924） | SMBIOS 字符串与 framebuffer 3392×2400（XN242 dual-DSI 2×1696，触控 display-coords 2400×3392 佐证；待实机 bootargs 确认） |

**不能据此声称**：erhai 的实际内存映射、面板时序、按键 GPIO 已验证。设备级
数据必须等 `tools/prepare_erhai.py` 用真实采集（`xbl_config_a.img`、原厂
`uefi_a.img`、`live.dtb`、`dtbo_a.img`）重新生成本包后才能作为证据。

## 存储栈（已编入，待实机验证）

FDF 现含 dodge 的时钟/供电依赖链（ULog → IPCC → CmdDb → PwrUtils → Npa →
Rpmh → Mailbox → Vcs → Clock → ICB，遵循 dodge 自身 APRIORI 顺序）与
UFSDxe + DiskIo/Partition/English/FAT 通用存储栈。Stage0BootManagerLib
会在横幅后枚举 BlockIO/SimpleFileSystem 并打印每个块设备容量与文件系统
数量，用于实机判断 UFS 是否打通。

**已知风险**：ClockDxe 会初始化 CESTA/media/DCD（字符串证据
`qcom,disp_cesta-pakala`、`ClockDCDEnableDependencies`），可能切断继承的
显示——黑屏不等于死机，45 秒自动重启触发即说明 DXE 仍在运行。Piano 用
“显示时钟租约”方案延后这些初始化，erhai 产品化时同样需要。
PMIC 依赖模块（PmicDxe/SdccDxe 等）仍排除在外。

FVMAIN 占用 3081K/3150K（99.9%）：storage 栈已接近 3 MiB FD 上限，
产品化（ProductCore/SimpleInit）前需要扩大 FD 并同步调整 BootShim
FD_BASE/FD_SIZE 与打包布局。

## 有采集后的再生成

```sh
python3 tools/prepare_erhai.py --capture <erhai采集目录> --extracted <解析输出>
```

接口与 `tools/prepare_piano.py` 相同：校验 manifest 的
`ro.product.device == 'erhai'`、核对四个固件镜像 hash、从 xbl_config 的
`/soc/memorymap` 重建 MemoryMapLib 与 native-memory-map.json。运行它会覆盖本
目录中同名的生成文件（MemoryMapLib.c、ConfigurationMapLib.c 等），dsc/fdf
中的设备身份部分保留手写。

## 构建

```sh
bash tools/build_stage0.sh erhai        # 在固件工作区内执行
```

产物：`artifacts/erhai/erhai-stage0.fd`（3 MiB FD）与 `BootShim.bin`。
与 piano 的 `MembootTest` 独立镜像同一用法：不含原厂 Android 内核，
`fastboot boot` 临时启动用于 bring-up，不写任何分区。
