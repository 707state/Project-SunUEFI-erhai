# erhai 主线内核启动现状与排查路径

本文记录 OnePlus Pad 2 Pro（erhai，SM8750）通过 SunUEFI Stage-0 启动
mainline Linux 的当前进展、已知阻塞和可选解决路径。不再使用 prev marker
一类临时标志位；后续按下面的路径逐项验证。

## 1. 已经确认可行的部分

- Stage-0 通过 `fastboot boot` 加载，读取 ESP `sda16` 中的
  `\EFI\BOOT\BOOTAA64.EFI`（内核 Image）、`erhai.dtb`、`initramfs.cpio.gz`。
- 固定物理放置：`kernel=0xA8000000`、`dtb=0xB0000000`、
  `initrd=0xB0200000`，与 Android 内核实际占用一致。
- 交接前停止 UEFI watchdog、halt UFS owner、发送 Gunyah watchdog
  disable SMC；跳过 EBS（erhai vendor EBS 回调在 Mu DxeCore 下地址未重定位）。
- 内核已经越过 `primary_entry`、`__primary_switched`、`start_kernel`、
  `setup_arch`，能输出 earlycon 日志。
- `/memory` 已按 Android live DTB 的 15 段固化；`splash_region` 为
  `0xFC800000 + 0x2B00000`，与 Android `/reserved-memory/splash_region`
  完全一致。
- `pmr735d@a`、`pm8010@d` 等 PMIC 探测失败会触发 SPMI 仲裁器通道状态
  WARN；当前把 `spmi@c400000` 整体禁用后，PMIC 相关报错消失。
- busybox 用户态已用 `aarch64-linux-gnu-gcc 16.2.0` 静态链接
  （GNU/Linux 3.7.0），initramfs 内提供 `ttyGS0` 与 `ttyMSM0` getty。
- 内核 Image 已改用主机 `/usr/bin/clang` 23.1.3 + `LLVM=1` 交叉编译，
  不再使用 NDK clang；NDK 仅用于用户态是错误做法，已废弃。

最近一次写入的 ESP：

```
ESP md5       25dd41b4f2845f55b815c1904a8893d4
Image         42473984 bytes（主机 clang 构建）
erhai.dtb     116824 bytes
initramfs     637867 bytes
```

## 2. 当前阻塞

内核在 `EFI services will not be available` 之后停止，未进入
`SunUEFI erhai busybox init`。同时日志出现：

```
efi: UEFI not found
ITS@...: Devices Table too large, reduce ids 32->19
```

说明：

- `efi: UEFI not found` 和 `EFI services will not be available` 是**预期**
  行为。前者来自 `drivers/firmware/efi/fdtparams.c:131`：内核解析
  `/chosen/linux,uefi-system-table`、`linux,uefi-mmap-*` 等 EFI 参数，
  raw 交接没有这些属性；后者来自 `arm_enable_runtime_services()` 的
  early_initcall。两者都只说明当前不是 EFI-stub 启动，不是卡死原因。
- `ITS ... reduce ids 32->19` 是 GIC ITS 设备表容量不足时的降级警告
  （`drivers/irqchip/irq-gic-v3-its.c:2535`），不是挂起原因；它反而说明
  GIC/ITS 已经初始化到较后阶段。
- 真正阻塞点在 `arm_enable_runtime_services()` 这个 early_initcall 之后、
  `populate_rootfs()`/`run_init_process("/init")` 之前或之中。之前的
  prev marker 写入本身曾引入同步异常，已全部移除，后续不再使用。

## 3. 已收集的参照事实

### Android live DTB / proc/iomem

- 根 `/memory` 15 段与当前 DTB 一致。
- `splash_region = 0xFC800000 + 0x2B00000`。
- `botloader_mem@0x81CE4000`（UEFI log）在 `0x81CE4000 + 0x10000`，
  正好落在 `/memory` 的 `0x81C00000-0x81CF4000` 空洞中；内核线性映射
  不可访问，不能用作标记。
- Android 有两个 SPMI 控制器：
  - `qcom,spmi@c42d000`：主 bus，`bus-id 0`，子节点包括
    `pmk8550@0`、`pm8550@1`、`pm8550vs@2-6/9`、`pm8550ve@3/5/6/8`、
    `pmd802x@4`、`pmih010x@7`、`pm8010@c/d`、`pmr735d@a`；
  - `qcom,spmi@c432000`：`bus-id 1`，`status = "disabled"`。
- mainline `sm8750.dtsi` 的 `spmi@c400000` 使用相同的 core/chnls/obsrvr/
  intr/cnfg 区域，但 PMIC 子节点集合与 Android 并不完全一致。

### LineageOS erhai cmdline

`device/oneplus/sm8750-common/BoardConfigCommon.mk`：

```
console=ttynull log_buf_len=2M nohugevmalloc nosoftlockup
qcom_geni_serial.con_enabled=0
sysctl.kernel.firmware_config.force_sysfs_fallback=1
```

另有 `androidboot.memcg=1`、`androidboot.serialconsole=0`、
`androidboot.usbcontroller=a600000.dwc3`、`androidboot.vendor.qspa=true`。
LineageOS 使用 GKI + `kernel/oneplus/sm8750` 的 vendor DTB，不依赖
earlycon/串口输出，而是靠 pstore/ramoops/logd 记录早期日志。

## 4. 可能路径（逐项验证，不写 marker）

### A. 继续 raw 路径，修复 init/DTB

1. 保留 skip-EBS，接受 `efi: UEFI not found`。
2. 用 `earlycon=efifb`/`console=tty0` 观察，确认卡在
   `populate_rootfs` 还是 `/init`。
3. 简化 `/init`：先只 `mount -t proc`、`mount -t sysfs`，不碰 configfs/
   USB，确认 `/bin/sh` 能启动；再逐步加回 USB ACM 和 ttyMSM0。
4. 用 GNU 静态 busybox 的 `sh` 直接作为 `init`（`exec /bin/sh`），
   排除 init 脚本本身问题。
5. 检查 `/dev/console`、`/dev/ttyGS0`、devtmpfs 是否建立；必要时把
   console 固定到 `ttyMSM0`。

### B. 对齐 Android/LineageOS 的 vendor DTB

1. 把 `spmi@c400000` 替换为 Android 的 `spmi@c42d000` 布局：
   `reg = <0xc42d000 0x40000>, <0xc400000 0x3000>, <0xc500000 0x400000>,
   <0xc440000 0x80000>, <0xc4c0000 0x10000>`；`bus-id = 0`。
2. 按 Android 的子节点集合补齐/修正 `pm8010@c/d`、`pmd802x@4`、
   `pmih010x@7`、`pmk8550@0`、`pm8550ve@8`、`pm8550vs@2-6`。
3. 对比 `its`、`psci`、`timer`、`chosen`、`reserved-memory` 的
   `compatible`/`reg`/`interrupts`；优先采用 Android live DT 的值。
4. 用 `console=ttyMSM0` 或 `console=ttynull` + pstore 复现，区分
   “卡在驱动”与“卡在 console”。

### C. 走真正的 EFI 交接（EBS + EFI system table）

1. 修复 erhai vendor EBS 回调在 Mu DxeCore 下的绝对地址/PE 重定位问题，
   正常调用 `ExitBootServices`。
2. 把 EFI system table 通过 `/chosen/linux,uefi-system-table` 传给内核，
   让 `efi: UEFI not found` 消失、EFI runtime 可用。
3. 参考 `docs/piano-efi-app-os-lease-split.md`、
   `docs/piano-product-owners.md`、`docs/piano-native-late-handoff.md`
   的 owner 退役顺序。
4. 这条路径能同时解决 PMIC/regulator、UFS、USB 对 EFI runtime 的依赖，
   但风险是 vendor EBS 回调问题必须彻底修好。

### D. 以 EFI 应用方式启动内核 Image

1. 不跳过 EBS，由 UEFI `LoadImage`/`StartImage` 加载
   `BOOTAA64.EFI`（内核 Image 带 EFI stub）。
2. EFI stub 会自行调用 EBS 并填充 EFI system table；内核走标准 EFI 启动。
3. 需要确认 erhai 的 EBS 回调缺陷是否只在 Mu DxeCore 的 PE 重定位下出现；
   可参考 piano 的 EFI app 路径和 `docs/piano-efi-app-os-lease-split.md`。

### E. 通过 bootloader 启动

1. 在 ESP 放置 GRUB/systemd-boot/自研 bootloader，由它加载内核并完成
   EFI handoff。
2. 优点是把 EBS/EFI system table 交给成熟 bootloader；缺点是多一层启动
   链，需要 UEFI 的块设备/文件系统驱动可用。

### F. 用户态/rootfs 路径

1. 当前 initramfs 是 RAM-only；先确保 `busybox sh` 能跑。
2. 需要持久 rootfs 时，再用 UFS LUN0 的 `sunuefi_root`（必须 arm64）
   挂载并 `switch_root`；注意 UFS/PMIC regulator 依赖。
3. 也可用 USB ACM 或 ttyMSM0 提供登录 shell，避免依赖显示与 UFS。

### G. 诊断通道

1. `console=ttyMSM0,115200n8` + `earlycon=msm_geni_serial,0xa9c000`
   （需要 UART）。
2. `pstore/ramoops` + `console=ttynull`，复刻 LineageOS 的日志路径。
3. Android 侧 `/sys/fs/pstore`、`last_kmsg`、`/proc/iomem`、
   `/sys/firmware/devicetree/base` 作为对照。

### H. PMIC/SPMI 与 ITS

1. 当前整体禁用 SPMI 是为了先拿到 shell；长期应按 Android 的
   `spmi@c42d000` + 子 PMIC 集合恢复。
2. `ITS ... reduce ids 32->19` 只影响 MSI 设备表容量；若后续 PCIe/MSI
   异常，再检查 `msi-controller@16040000` 的 `reg`/`#msi-cells` 与
   Android 的 ITS 节点。

## 5. 约束与决策

- 不再添加任何 prev marker/阶段标志位；诊断只依赖 earlycon、串口、
  pstore、Android 侧 DTB/日志。
- 内核用主机 `/usr/bin/clang` 23.1.3 + `LLVM=1`；用户态用
  `aarch64-linux-gnu-gcc` 静态链接；不使用 NDK 编译内核。
- 每次只改一个变量，记录 ESP/Image/DTB/initramfs 的 md5，并保留可回退
  的 ESP 镜像。

## 6. 下一步

优先执行 A（简化 `/init` 并确认 `populate_rootfs`/`run_init_process`）
和 B（对齐 Android vendor DTB 的 SPMI/PMIC/ITS）。如果 A 证明内核能进
shell，再逐步恢复 C/D 的 EFI 交接。
