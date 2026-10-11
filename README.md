# Project SunUEFI

小米平板 8 Pro（代号 `piano`，骁龙 8 至尊版 / SM8750）的统一 UEFI 与 Linux 适配项目。

目标是让这块平板通过 UEFI 获得完整的 Linux 和 Windows 体验，同时保留原厂 Android。现在已经可以从内部存储运行 Debian / GNOME，并在 Android 与 Linux 之间切换；Windows 尚未实现。

[开始使用](#开始使用) · [功能与已知问题](#功能与已知问题) · [故障恢复](docs/user/recovery.md)

## 现在能做什么

从内部磁盘启动的 Debian 13 / GNOME 已经可以：

* 用 Adreno GPU 加速绘制桌面，屏幕以 3200×2136、默认 120 Hz 显示，可以手动调亮度。
* 用手指点按、拖动窗口、长按弹出菜单和使用手势。
* 接上官方磁吸键盘打字、使用触控板，键盘背光可以通过 GNOME 的标准选项调节。
* 使用 Wi-Fi 和蓝牙。
* 用四个扬声器播放声音，并用 GNOME 夜灯调整屏幕色温。
* 打开前后相机预览并录像。
* 使用默认中文界面、拼音输入法和 Chromium 浏览器，GNOME 动画默认开启。
* 在 Linux 和原厂 Android 之间来回切换。

目前最影响使用的是：启动到桌面通常需要一到两分钟，途中可能长时间灰屏或白屏，缺少清楚的进度提示；切换刷新率、关屏或合盖之后，屏幕可能黑屏而背光仍亮，需要重启。完整列表见下文[功能与已知问题](#功能与已知问题)。

## 实机画面

| | |
| --- | --- |
| ![Android 与 UEFI 开机选择](docs/assets/gallery/boot-selection.webp)<br>开机选择 | ![TianoCore 启动画面](docs/assets/gallery/uefi-splash.webp)<br>UEFI 启动画面 |
| ![GNOME 系统信息与相机预览](docs/assets/gallery/gnome-camera.webp)<br>Debian / GNOME 与相机 | ![Android 引导助手界面预览](docs/assets/gallery/android-boot-helper-preview.webp)<br>Android 引导助手 |


| Minecraft · Vulkan / Turnip | Minecraft · OpenGL / Freedreno |
| --- | --- |
| ![原版 Minecraft 26.3 Vulkan 实机画面](docs/assets/gallery/minecraft-vulkan.webp) | ![原版 Minecraft 26.3 OpenGL 实机画面](docs/assets/gallery/minecraft-opengl.webp) |

两张均为原版 Minecraft 26.3，无优化模组，在平板 3K 屏幕上运行。照片分别显示约 446 FPS 和 174 FPS，为不同场景的瞬时读数。Vulkan 的分辨率显示差异可能与驱动 bug 或 Wayland 缩放有关。

## 开始使用

准备一台已解锁 Bootloader、已安装 KernelSU／SukiSU 的小米平板 8 Pro，并备份重要数据。日常安装在平板上的 **SunUEFI 引导助手** 中完成，无需电脑命令或填写序列号。

1. **下载三个包**：从 [Build products](https://github.com/slwyts/Project-SunUEFI/actions/workflows/build-products.yml) 的完整构建下载引导助手、ESP 和 Root。解压后得到模块 ZIP、`esp.img`（启动镜像）和 `root.ext4.img`（系统镜像）。
2. **安装并打开助手**：在模块管理器中安装模块 ZIP，提示出现时按音量＋安装引导；重启后，点击模块卡片上的 **打开 WebUI**。
3. **分配存储空间**：在 **存储 → 创建独立分区** 中选择容量并确认，完成后重启。已有独立分区时跳过这一步。
4. **刷写镜像**：在 **存储** 页分别选择下载好的 `esp.img` 和 `root.ext4.img`，点击 **安装所选镜像**，等待完成。
5. **进入 Linux**：在 **启动** 页选择 **UEFI → Linux**，点击 **保存并重启**。

首次分区尚未完成实机验证，相关限制见[已知问题](docs/user/known-issues.md#首次分区)。详细操作见[安装指南](docs/user/install-from-artifact.md)；[电脑安装](docs/user/install-from-computer.md)与[临时启动](docs/user/temporary-boot.md)作为备用方式保留。Magisk 用户打开助手的方法见[模块说明](docs/devel/android-module.md)。

Linux 默认用户名为 `piano`，密码为 `piano123456`，桌面自动登录。首次进入桌面通常需要一到两分钟。

HyperOS 更新后，在助手的 **维护** 页点击 **重新安装引导**。想回 Android，可以在开机选择页按音量＋选择 Android、短按电源确认；Linux 下也可运行 `piano-next-boot android --reboot`。遇到无法启动的情况，见[故障恢复](docs/user/recovery.md)。

## 开机时发生什么

SunUEFI 把开机选择器、原厂 Android 内核和 UEFI 核心放在同一个启动镜像里，称为**合体 BOOT**。开机时选择 Android 就进入 HyperOS；选择 UEFI 可以启动 Linux 或进入固件菜单。小米原厂的底层引导程序和 Recovery 保留。

```mermaid
flowchart LR
    A["原厂引导程序<br/>XBL / ABL"] --> B["合体 BOOT 中的选择页<br/>Android / UEFI · 3 秒"]
    B -->|"Android"| C["原厂 Android 内核<br/>HyperOS"]
    B -->|"Linux / 菜单"| D["SunUEFI (EDK2)"]
    D --> E["ESP 中的 Linux 内核<br/>sunuefi_esp"]
    E --> F["ext4 根文件系统<br/>sunuefi_root"]
    F --> G["Debian / GNOME"]
    D -.->|"目标，尚未实现"| H["Windows"]
```

开机选择页停留约三秒，音量键切换，短按电源确认；没有操作时沿用保存的选择。Android 路线直接进入原厂内核，Linux 路线由 UEFI 从独立存储分区加载系统。实现细节见[合体 BOOT](docs/devel/android-boot-repack.md)。

### 启动路线会被记住

在助手中保存的选择会成为默认启动路线；开机时用音量键临时改选，不会改变这个默认值。首次安装默认进入 Android。

* 在 Android 的 SunUEFI 引导助手中选择 Android 或 UEFI，保存并重启；已安装 Linux 时可以直接进入 Linux。
* 在 Linux 里执行 `piano-next-boot android --reboot` 回到 Android。
* 用 Android 标准的 `reboot recovery` 进入小米原厂 Recovery，不改动已保存的路线。

引导助手提供中文／英文界面。HyperOS 更新后，点击 **重新安装引导** 即可使用新系统的内核恢复引导，保留启动选择；目前需要手动点击。使用方法见[模块说明](docs/devel/android-module.md)，底层实现见[启动状态与切换](docs/devel/reboot-request.md)。

### UEFI 里的 Fastboot

SunUEFI 运行时在后台提供一个 USB Fastboot 服务，设备名为 `SunUEFI-piano`。它用来调试：即使屏幕没有画面，也能从电脑读取日志、导出屏幕截图、只读读取分区内容，或请求启动已安装的 Linux。

它和另外两个容易混淆的东西不同：

| 名称 | 什么时候出现 | 能做什么 |
| --- | --- | --- |
| 原厂 Bootloader Fastboot | 开机时按键进入，或 `adb reboot bootloader` | 小米原厂功能，可刷写分区 |
| UEFI Fastboot（本项目） | SunUEFI 运行期间 | 日志、截图、只读读取、请求启动；不支持刷写和擦除 |
| Linux 的 USB 网络 / SSH | Linux 启动之后 | 普通的 Linux 远程登录与调试 |

交接给 Linux 时 UEFI Fastboot 就会退出。从冷启动的合体 BOOT 进入时，USB 初始化曾经超时，所有后台场景也还没有走通，所以现在不能依赖它。命令见 [UEFI Fastboot](docs/user/fastboot.md)。

### 同一个核心

项目只维护一套 UEFI 核心和一套产品镜像 `PianoUEFI-product.img`。合体 BOOT、临时的 `fastboot boot` 启动、安装器用到的都是同一份核心，不再按功能拆成多套测试镜像。

## 功能与已知问题

当前运行使用 CSOT 面板配置，选择方式见[面板选择](docs/devel/panel-selection.md)。

### 可以使用

| 功能 | 说明 |
| --- | --- |
| 磁盘启动 Linux | 从合体 BOOT 经 UEFI 进入内部 ESP 和 ext4 系统，普通重启沿用选择 |
| 返回原厂 Android | Android 路线直通，Linux 里可以一条命令切回 |
| GNOME 桌面与 GPU 加速 | 原生双 DSI 显示、Adreno 加速 |
| 屏幕显示与亮度 | 3200×2136、默认 120 Hz 显示，手动亮度 |
| 触屏 | 点按、拖动、长按、手势，报告速率约 144 Hz |
| 官方键盘与触控板 | 打字、指针、点击、键盘背光 |
| Wi-Fi | 扫描并联网 |
| 蓝牙 | 可用 |
| 触控笔 | 默认 120 Hz 下定位与压感绘画，轻捏按键和笔端震动 |
| 四扬声器 | 沿用原厂功放增益，音量可调 |
| 夜灯 | GNOME 夜灯调整色温 |
| 前后相机 | 预览和录像；画质问题见下表 |
| 电池与充电状态 | 双电池、基础 PD 充电和 UPower 状态 |
| 默认桌面 | 中文界面、拼音输入法、Chromium 浏览器，GNOME 动画开启 |

Linux 默认用户名：`piano`，密码：`piano123456`。桌面自动登录，锁屏和 `sudo` 使用此密码；修改密码可运行 `passwd`。

### 可以使用，但有问题

| 功能 | 能做到什么 | 现存问题 |
| --- | --- | --- |
| 刷新率 | 默认 120 Hz，保留其他显示模式 | 切换刷新率，或关屏、合盖后恢复，可能黑屏、背光仍亮 |
| 相机 | 前后摄像头预览与录像，走高通 CAMSS/TFE 硬件 ISP，3A（自动曝光 / 对焦 / 白平衡）由软件实现 | 首帧、曝光、对焦和画质仍在改善，与原厂有差距；完整专业模式尚未提供 |
| 麦克风 | 录音、回放，原生单声道输入 | 底噪和桌面输入电平仍需调校 |
| 传感器 | 加速度、光线、罗盘、距离等数据可读，并接入标准的 SensorProxy | 自动旋转、自动亮度曲线等桌面联动还在完善；陀螺仪等数据的单位和标准接口未全部完成 |
| 电源 | 充电状态、基础 PD 充电 | 关机偶尔回到 Android，原因未知；合盖和电源键目前只处理背光 |
| 启动时间 | 能从内部 ext4 启动到桌面 | 通常需要一到两分钟，启动过程中可能灰屏或白屏、没有进度提示；首次启动可能更久 |
| UEFI 菜单 / 固件设置 | SimpleInit 中文菜单可显示、可操作，能进入 TianoCore 设置 | 设置页的布局和输入仍在完善；UEFI 阶段触屏和键盘尚未接通 |
| UEFI Fastboot | 日志、截图、只读读取 | 冷启动 USB 初始化曾超时，部分后台场景未走通 |
| 闪光灯 / RGB 灯 / 红外 | 闪光灯的标准 LED 接口已注册；RGB 灯接口已出现 | 闪光灯实际发光未观察；红外发射未验证 |

### 尚未完成

* **触控笔的扩展功能**：144 Hz 笔输入、完整掌压处理、笔身滑动、应用快捷手势和无线充电仍在完善。运行中切换刷新率仍可能黑屏。
* **睡眠**：锁屏、待机、休眠没有完成。
* **HDR、12-bit 色深、可变刷新率**：还在适配，不能作为可用功能。
* **指纹**：原厂安全通路已定位，Linux 端没有实现。
* **Windows / WinPE**：目标，尚未启动。

更详细的现状和设备记录见[项目状态](docs/status.md)，用户可见的问题和绕过办法见[已知问题](docs/user/known-issues.md)。

## 自己构建

先取得固定版本的上游源码，再在构建容器里生成 UEFI 和安装工具包：

```sh
./build.sh sources
docker build -t sunuefi-builder -f containers/Dockerfile .
docker run --rm -v "$PWD:/workspace" -w /workspace sunuefi-builder bash -euc '
  git config --global --add safe.directory /workspace
  git config --global --add safe.directory "/workspace/*"
  python3 -m venv .venv
  source .venv/bin/activate
  python -m pip install -r requirements-build.txt
  ./build.sh uefi
  ./build.sh module
  ./build.sh installer --product artifacts/product/PianoUEFI-product.img --output artifacts/installer-uefi
'
```

模块输出在 `artifacts/android/`，与 `artifacts/product/` 中的 UEFI 使用同一份核心。GitHub Actions 的 `uefi` 和 `debian-gnome` 目标都会同时构建并上传模块。

完整的 Debian / GNOME 系统包需要 ARM64 构建机，以及允许 chroot 的 `--privileged` 容器，依次运行 `./build.sh linux`、`mesa`、`sensors`、`release-rootfs`、`package`、`installer`。需要主机检查时手动运行 `./build.sh check`，不用连接平板。完整流程、输入材料和各步骤的输出位置见[构建手册](docs/devel/building.md)与[公开构建链](docs/devel/public-build.md)。

## 项目站在哪些项目之上

SunUEFI 不是从零重写所有东西。上游源码保持固定版本放在 `upstream/`，我们自己的修改放在 `patches/`、`uefi/`、`linux/` 和构建工具里。

| 项目 | 负责什么 | 我们在上面做了什么 |
| --- | --- | --- |
| [TianoCore EDK2](https://github.com/tianocore/edk2) / [Mu-Silicium](https://github.com/Project-Silicium/Mu-Silicium) | UEFI 基础代码和高通平台集成 | Piano 平台、启动交接、存储、输入、显示继承、UEFI Fastboot，以及对原厂分区的只读保护和安全退出 |
| [simple-init](https://github.com/BigfootACA/simple-init) | UEFI 里的图形启动菜单 | 平板布局、中文、实体按键，以及和固件后台服务的联动 |
| [linux-piano](https://github.com/blu-sharky/linux-piano) | SM8750 内核、设备树和板级驱动基线 | 合入稳定版内核，修正内存 / DMA、显示、音频等 |
| [debian-piano](https://github.com/blu-sharky/debian-piano) | Debian 根系统构建、设备服务、相机和输入辅助程序 | 专用 ESP / root、多发行版 BSP（板级配置包）、统一打包 |
| [piano-firmware](https://github.com/blu-sharky/piano-firmware) | Wi-Fi、蓝牙、DSP、触控等设备固件和功放预设 | 按来源装入系统；设备参数从本机原厂数据读取。它不是整套 HyperOS，也不含原厂相机算法 |
| [piano-mesa](https://github.com/blu-sharky/piano-mesa) | Adreno 图形用户空间 | 与内核和根系统配套的 Mesa 软件包 |
| [piano-sensors](https://github.com/blu-sharky/piano-sensors) | 高通传感器（SSC）通信与桌面桥接 | 数据类型、读取接口、标准 SensorProxy 与自动亮度策略 |
| [MiCode](https://github.com/MiCode) 与本机 HyperOS | 原厂设备树、驱动、配置的参照 | 对照真实命令、时序和参数，按标准接口移植 |

内核之外的 Debian 软件仍通过标准 APT 安装。除 Debian / GNOME 之外，仓库里还有 Ubuntu、Arch、KDE 等目标的配置，但只有 Debian / GNOME 完整走通过，其他目标没有验证。

## 文档

* [快速上手](docs/user/getting-started.md)、[安装指南](docs/user/install-from-artifact.md)、[返回 Android](docs/user/recovery.md)、[UEFI Fastboot](docs/user/fastboot.md)、[已知问题](docs/user/known-issues.md)
* [项目状态](docs/status.md)：功能和设备记录
* [文档导航](docs/README.md)：开发、专题和历史记录
* [第三方来源](THIRD_PARTY.md)、[许可](LICENSE.md)、[贡献指南](CONTRIBUTING.md)、[AI 协作规范](AGENTS.md)
