# UEFI 物理白屏对照

SimpleInit 能生成完整帧缓冲截图，但物理面板白屏。已确认可显示的旧版不启动当前原厂驱动基础层，因此不能只替换 SimpleInit 就认定问题已经解决。

对照使用同一产品核心，不关闭 UFS、USB 或替换 GOP。显式启用诊断时，通过启动日志所用的同一个 GOP 绘制彩色背景和阶段名称，每个检查点保留三秒：

| 阶段 | 背景 | 作用 |
| --- | --- | --- |
| BEFORE DRIVERS / BEFORE CLOCK | 蓝 | 原厂驱动基础层、ClockDxe 启动前 |
| AFTER CLOCK | 绿 | ClockDxe 返回后 |
| AFTER DRIVERS | 橙 | 基础层全部返回后 |
| AFTER UFS | 紫 | 存储初始化后 |
| BEFORE SIMPLEINIT | 青蓝 | 调用菜单前 |

每次绘制记录 `PIANO_DISPLAY_CHECKPOINT`，包括阶段、GOP 地址、帧缓冲地址、模式和返回状态。绘制成功仅证明 GOP 接受操作，是否实际扫描到屏幕仍需观察物理面板。

```sh
SUNUEFI_DISPLAY_DIAGNOSTIC=1 ./build.sh uefi
```

该开关默认关闭，构建清单记录 `display_diagnostic` 与编译标志。普通构建不增加色块或等待，也不改变功能集合。测试选择菜单路线 `target=1`，不自动启动 Linux；原厂 Android 直通和 Recovery 行为保留。

如果色块在进入 SimpleInit 前消失，先定位最后可见的驱动阶段；如果所有检查点均可见、菜单接管才消失，再针对 SimpleInit 的 GOP 选择、模式和刷新路径做对照。RAM 截图不能代替物理屏幕结果。

## 2026-10-12 实际结果

同机通过合体 BOOT 的菜单路线运行后，物理屏幕可见蓝色的 `BEFORE DRIVERS` 和
`BEFORE CLOCK`，随后变灰；绿色 `AFTER CLOCK` 与后续检查点不可见。
原厂 Fastboot 的 `continue` 返回 Android 后，保留日志显示 SimpleInit 已选择
`0xFC800000`、3200×2136 的 GOP，持续绘制菜单，并收到实体按键事件。

因此这一轮失效发生在 ClockDxe 的 StartImage 区间，早于 SimpleInit；后续固件仍
执行到了 GUI，不是 ClockDxe 卡死阻止菜单运行。早期记录被大量 UFS 读日志覆盖，
当前结果尚不能定位到 ClockDxe 内部某一次寄存器或供电操作。
软件菜单截图正确也不能证明物理显示链路仍工作。

本次 FD 摘要为 `db047de7f642c70dfa1c7a459d4913c9a56117ecc88f8508f0c2a18097c69687`，
构建标识 `de7b9504-a576-4406-83f8-3f6290fda743`。恢复后启动偏好已改回 Android。

## 已排除的单独改动

原厂 `ClockDxe.efi` 的 `0xc540` 初始化先调用 `0xc420` 申请每个 CESTA
实例的临时时钟，再检查 `+0xd8` 的自动初始化标志。之前只清除了显示实例的
自动初始化标志，因此仍会通过正常 EnableClock 路径申请四个显示时钟，
涉及父时钟与供电状态；“没有执行 CESTA 表配置”不等于“没有改动显示链路”。

候选在派生的驱动副本中，将显示实例 `0x3e4b0` 的 `+0xd0` 申请数量从 4
改为 0，延后 `gcc_disp_ahb_clk`、non-GDSC AHB、RSCC AHB／VSYNC 的启动期申请。
保留其名称表、运行时协议和其他实例；后续真正使用这些资源的驱动仍通过正常
接口申请。它不禁用 ClockDxe、UFS 或 USB，也不向显示寄存器写入猜测值。

派生驱动总计与原件相差七字节，SHA256 为
`98d76405ef517901ea593d5207afb2daf39c520c35b28f4e776271111200a24d`。
同机再次运行后仍只看到 `BEFORE CLOCK`，没有绿色 `AFTER CLOCK`。
这处改动已撤回默认源码：延后四个申请本身不足以修复显示，不能把它当成根因。
检查点结果现在保存在产品内存中，由现有 GUI pump 周期性重发，避免被 UFS
日志覆盖；默认非诊断构建没有这一输出。

## ClockDxe 内部对照

`SUNUEFI_DISPLAY_DIAGNOSTIC=2` 在模块初始化、核心初始化、供电资源注册、
显示模块地址整理、显示源注册、CESTA 和 CPU 初始化的八个固定调用点前后绘制检查点。
显示模块的两个调用点只观察实际 display 模块，其他模块原样调用，不增加等待。

诊断在 LoadImage 前修改拥有的临时文件缓冲：使用原件中已确认的零填充可执行空间
放置转接指令，检查每个原始 BL 目标以及重定位条目，将调用转到产品中的包装函数。
包装函数原样转发八个整数参数、调用原始目标，并原样返回结果。
它不修改采集文件、不写正在运行的驱动代码、不更换时钟协议，也不直接操作硬件寄存器。
载入后使用 LoadedImage 提供的实际基址定位原调用，构建清单记录 `clock_init_trace`。

这种插桩会延长初始化时间，只用于定位；`SUNUEFI_DISPLAY_DIAGNOSTIC=0` 的默认
发布构建完全不包含这些转接或停顿。实机结果仍以最后可见检查点和保留日志交叉判断。
