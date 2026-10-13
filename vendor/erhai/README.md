# erhai 板级输入

## board.dtb

实机运行时 FDT（`/sys/firmware/fdt`），来自用户的 OnePlus Pad 2 Pro
（`oplusboot.prjname=24926`，XN242 面板 `PanelID-0x00C18000`）。SHA256 与
来源记录在 `manifest.json`。

用途：替代 dodge DTB 作为 UEFI 阶段传给原生驱动（ClockDxe/VcsDxe/UFSDxe）
的设备描述——时钟/电压/存储驱动由此读到本机真实节点（显示 CESTA
`/soc/qcom,sde_cesta@0x0af30000`、UFS `/soc/ufsphy_mem@1d80000`）。

边界：含运行时快照值（`/chosen` 的 initrd/kaslr-seed），交接 Linux 前必须
剥离（piano 同款处理，见 `tools/prepare_piano.py` 的 live DTB 清洗）；
本文件不是完整原厂采集（xbl_config/uefi 镜像仍缺），内存映射仍是
dodgePkg 同 SoC 替身。
