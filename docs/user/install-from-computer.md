# 备用：从电脑安装

日常安装推荐使用平板上的[引导助手](https://github.com/slwyts/Project-SunUEFI/blob/main/docs/user/install-from-artifact.md)。本页适用于需要从电脑更新已有 ESP 和 root 分区的情况。

默认 GitHub Actions 的 **Build products** 提供三项下载：

| 下载 | 内容 |
| --- | --- |
| `SunUEFI-Android-Module-提交号` | 引导助手模块 ZIP |
| `SunUEFI-ESP-提交号` | ESP 镜像、固件和 `install.sh`／`install.cmd` 一键安装脚本 |
| `SunUEFI-Root-提交号` | `bundle/root.ext4.img`，完整 Debian / GNOME 根系统 |

将 ESP 与 Root 包解压到同一目录，合并 `bundle/`。安装脚本旁边应同时有 `bundle/esp.img` 和 `bundle/root.ext4.img`。不需要克隆仓库。手动选择 `uefi` 或 `linux` 构建只生成对应开发产物，不提供完整系统。

构建日志在另一个 `piano-build-records-目标-提交号` 里。

## 需要什么

* 电脑：Python 3.10+，Android platform-tools（`adb`、`fastboot` 在 PATH 中）。可以用 `SUNUEFI_PYTHON` 指定 Python。
* 平板：已解锁 Bootloader、已 root、开启 USB 调试，进入 Android。
* 平板里已有 `sunuefi_esp` 和 `sunuefi_root` 两个分区。安装器不能给全新平板分区，会返回 `NEW_INSTALL_NOT_READY`。

## 安装

连接平板并允许 USB 调试。脚本会自动识别唯一连接的小米平板 8 Pro，无需填写序列号。先生成安装计划：

```sh
sh install.sh plan --output update-plan.json
```

Windows 把 `sh install.sh` 换成 `install.cmd`。安装器自动使用旁边合并好的 `bundle/`。

确认计划后，加上 `--execute` 才会写入：

```sh
sh install.sh apply --plan update-plan.json --execute
```

只有同时连接多台小米平板时，才需要用 `--serial` 指定设备。

## 可选：启用 SSH 登录

如果希望从电脑远程管理 Linux，可以在安装时提供自己的 SSH **公钥**。安装器会把它加入平板的 `root` 和 `piano` 账户；不提供公钥也可以正常进入桌面。

创建安装计划和执行安装时，需要指定同一个 `.pub` 文件：

```sh
sh install.sh plan --ssh-public-key ~/.ssh/id_ed25519.pub --output update-plan.json
sh install.sh apply --plan update-plan.json --ssh-public-key ~/.ssh/id_ed25519.pub --execute
```

这项功能需要 Linux 电脑上的 `e2fsprogs` 和 GNU `coreutils`。Windows 使用 `install.cmd`，并在默认 WSL 发行版中安装这两个软件包。请只提供公钥，私钥始终留在自己的电脑上；发布镜像不包含任何人的登录密钥。

## 它会做什么

安装器核对 GPT、设备身份、分区容量和镜像哈希，只更新 `sunuefi_esp` 和 `sunuefi_root`，然后回到 Android 读回检查。当前 Linux 在启动时自动读取本机蓝牙地址，不需要安装器预先修改 ESP。

使用没有自动地址初始化的旧系统时，可以单独补上设备树地址。当前系统无需这一步：

```sh
sh install.sh provision-bluetooth
sh install.sh provision-bluetooth --execute --output device-provision.json
```

## 不做什么

* 不给新平板分区、不缩小 userdata。
* 不写 BOOT 分区。请通过[引导助手](install-from-artifact.md)安装引导。
* 不支持 `--recovery`，会被拒绝，原因见[返回 Android](recovery.md)。
* 出现问题时的退路见[返回 Android 与故障恢复](recovery.md)。
