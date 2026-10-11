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
