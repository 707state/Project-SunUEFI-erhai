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
