# HMCL 与 JavaFX 图形加速

HMCL 可以在平板上使用 ARM64 Java 运行。若启动器显示“软件渲染”，先区分启动器界面的 JavaFX 渲染与 Minecraft 自己的 OpenGL 渲染，两者是独立的。

当前 Azul Zulu JavaFX 的 Linux 驱动白名单没有包含 `freedreno`。即使 Mesa 已提供 Adreno 硬件加速，JavaFX 仍可能提示 `Failed Graphics Hardware Qualifier check`，随后使用 `SWPipeline`。

只给 HMCL 增加以下参数即可绕过这项厂商白名单检查：

```sh
java -Dprism.forceGPU=true -jar HMCL.jar
```

使用 HMCL 的 `.sh` 启动包时：

```sh
HMCL_JAVA_OPTS="-Dprism.forceGPU=true" bash HMCL.sh
```

Java 需包含适合 ARM64 的 JavaFX。不要在系统中全局设置 `JAVA_TOOL_OPTIONS`，也无需把显卡名称改成其他厂商。该参数不会强制 Minecraft 使用某个驱动，游戏仍按自己的 LWJGL/OpenGL 初始化流程运行。

定位问题时临时增加 `-Dprism.verbose=true`。当前平板的 HMCL 3.17.0.359 / Zulu Java 25.0.4.1 已从 `SWPipeline` 切换到 `ES2Pipeline`；Mesa `glxinfo -B` 同时报告 Adreno 830v1、`Accelerated: yes`。这是启动器 GPU 渲染的结果，不代表已测量游戏帧率。

JavaFX 的对应实现见 [X11GLFactory.java](https://github.com/openjdk/jfx/blob/master/modules/javafx.graphics/src/main/java/com/sun/prism/es2/X11GLFactory.java)。
