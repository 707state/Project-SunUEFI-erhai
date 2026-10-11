# UEFI 设置页面

SimpleInit 提供触摸尺寸的图形菜单；TianoCore 设置页面使用 UEFI 文本控制台。
在 3200×2136 屏幕上，默认 8×19 字形的 80×25 页面只占很小一块区域。

产品构建只为 GraphicsConsole 的字符绘制增加四倍缩放。该驱动内部使用
800×534 的逻辑画布，把文本块放大到物理屏幕；公开 GOP 的模式和帧缓冲仍是
3200×2136，SimpleInit、截图和操作系统使用原来的接口，不切换物理显示模式。
80×25 字符区放大后约为 2560×1900。

构建工作副本中的五组 HII 字符串加入 `zh-CN`，常用设置页面默认中文，
技术设备名和未翻译的文字仍用英文。中文来自 Noto Sans CJK SC 的 16×19
宽字符位图，通过标准 HII 字体包注册；构建记录源字体和生成文件摘要。
没有修改固定的上游检出目录。

实体按键通过标准 `SimpleTextIn` 协议接入。按键设备同时安装
`ConsoleInDevice` 标记，让标准控制台汇聚驱动能够找到它。进入设置页时，
音量键映射上下移动，短按电源映射回车，同时按音量两键映射返回。

源码位置：

* `uefi/components/product-support/Library/ConsoleScale/`：缩放和中文翻译。
* `tools/prepare_product_console.py`、`tools/prepare_product_ui.py`：在构建副本生成字形并接入 GraphicsConsole。
* `tools/prepare_gui_profile.py`：默认语言。
* `uefi/core/PianoKeys.c`：标准按键设备与映射。

当前已完成正式产品编译，并用同一缩放代码与生成的字形导出 3200×2136
预览，检查文字大小、宽字符和英文混排。这只是字形及比例检查，
不是设置应用的实机截图；放大后的实际布局与按键仍需下一次设备验证。
触屏、磁吸键盘及触控板的 UEFI 驱动尚未完成，Linux 下可用不等于 UEFI 下可用。
