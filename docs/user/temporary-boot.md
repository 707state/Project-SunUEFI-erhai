# 备用：临时启动固件

日常安装使用[引导助手](install-from-artifact.md)。本页只介绍临时运行固件：
用 `fastboot boot` 将它载入内存，重启后恢复原来的启动方式。

临时加载本身不会把 Linux 装进平板。要进入 Linux，平板里需要已经有 `sunuefi_esp` 和 `sunuefi_root`。

Linux 默认用户名为 `piano`，密码为 `piano123456`，桌面默认自动登录。锁屏解锁和 `sudo` 使用这个密码；可在系统设置中修改，或运行 `passwd`。

## 准备

* 小米平板 8 Pro，Bootloader 已解锁，重要数据已备份。
* 电脑装有 `adb` 和 `fastboot`，以及一根 USB-C 数据线。
* `PianoUEFI-product.img`：来自 GitHub Actions 的 `uefi` 或 `debian-gnome` 构建，或自行构建（见[构建手册](../devel/building.md)，输出在 `artifacts/product/`）。可以用 `sha256sum` 与来源提供的值核对。

## 1. 进入原厂 Fastboot

在 Android 里打开“USB 调试”，连接电脑：

```sh
adb reboot bootloader
fastboot devices
fastboot getvar unlocked      # 应显示 unlocked: yes
```

## 2. 临时加载固件

```sh
fastboot boot PianoUEFI-product.img
```

返回 `OKAY` 只说明原厂 Bootloader 接受了镜像。接着确认固件是否真的运行：

```sh
fastboot -s SunUEFI-piano getvar product     # 应返回 piano-sunuefi
```

UEFI 菜单白屏已修复。USB 调试接口仍可能无法连接；查询失败时不要据此判断固件一定没有运行。

## 3. 启动 Linux 或回到 Android

已经安装了 Linux 时，请求从 ESP 启动：

```sh
fastboot -s SunUEFI-piano oem boot-stable
```

固件读取 ESP 里的 `\EFI\Piano\stable\boot.img` 并交接给 Linux，到桌面约需要两分钟。

不想继续时，退出并回到 Android：

```sh
fastboot -s SunUEFI-piano reboot
```

## 遇到问题

* 屏幕白屏、没有响应：先执行 `getvar product`，再看[已知问题](known-issues.md)。
* 卡住或电脑看不到设备：见[返回 Android 与故障恢复](recovery.md)。
* 需要日志和截图：见 [UEFI Fastboot](fastboot.md)。
