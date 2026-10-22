# 代码导航与维护边界

## 目标与入口

| 板型 | 构建脚本 | 板级配置 | 驱动目录 | 打包工具 |
|---|---|---|---|---|
| SF32LB527 / E0470A03 | `build-sf32lb527.ps1` | `epdiy-epub/sf32lb527-oed-e0470a03/` | `epdiy-epub/src/boards/sf32lb527/` | `tools/package_527.py` |
| SF32LB57GY / E0470A03 | `build-sf32lb57gy.ps1` | `epdiy-epub/sf32lb57gy-oed-e0470a03/` | `epdiy-epub/src/boards/sf32lb57/` | `tools/package_57.py` |

两个目标均启用 `READER_E0470A03`。57 板另以
`BSP_USING_BOARD_SF32LB57GY_OED_E0470A03` 选择驱动。
原有 `sf32-oed-epd_v11/v12/v12_spi` 目标保留原驱动路径。

## 阅读与显示

`epdiy-epub/src/main.cpp` 管理阅读状态、消息队列、章节预取和空闲休眠。
`epdiy-epub/lib/Epub/EpubList/` 负责 EPUB 解析和分页；
`src/font_manager.c`、`src/ft_dfs_bridge.c` 与 `src/reading_settings.cpp`
负责字体、文件读取桥接和设置保存。

`src/boards/reader/reader_ui.cpp` 是双板共用菜单、前光设置和升级页面；
`reader_board.h/.cpp` 将前光、触摸、存储等待、时钟待机和诊断接口分发到板级实现。
修改菜单先看共用层；修改引脚、电源时序或唤醒先看对应板级目录。

板级 `SF32Paper*.cpp` / Renderer 接口连接应用与显示驱动。
527 显示驱动位于 `sf32lb527/epd_e0470a03.c`，57 位于
`sf32lb57/panel/`。两板电源芯片、触摸接线和 TF 供电能力不同，
不能直接共用 GPIO 或低功耗关断序列。
原厂波形数据位于 `epdiy-epub/waveform/e0470a03/`。

## 启动、恢复与固件包

`epdiy-epub/project/SConstruct` 为新板建立应用、bootloader、recovery 和分区表。
`epdiy-epub/dfu527/` 是双板共用的受保护升级实现，目录名因历史兼容保留：

- `boot/`：启动校验、恢复按键检测和板型配置。
- `common/`：分区、板型 ID、协议和镜像校验。
- `loader/`：USB CDC 恢复；527 使用恢复提示屏，57 关闭独立电源使能。

两板应用、字体、波形地址相同，但板型 ID 分别为 `0x05270470` 和
`0x05700470`。构建、工厂包和升级包必须按板型配套。
`tools/usb_dfu_527.py` 提供共用协议，`tools/usb_dfu_57.py` 指定 57 板身份。
Windows 图形工具的原生 USB 模式目前只支持 527，57 使用其 CLI。
工厂首刷与日常升级步骤见对应板型文档。

## 构建与检查

首次获取代码需执行 `git submodule update --init --recursive`，
再按 SiFli-SDK 安装说明准备锁定工具链。Windows 在仓库根目录运行对应构建脚本。

主机检查统一使用：

```console
python tools/run_host_tests.py
```

依赖 Python 3、gcc、g++，失败时返回非零状态并列出失败命令。
覆盖波形、分页、字体、设置、电源策略、恢复和烧录包/协议检查。
CI 单独运行这套检查，并构建全部五个目标，为两种新板生成升级与工厂包。
主机检查和编译通过不代表合并版本已经完成实机显示、升级、唤醒或电流验证；
历史实机范围见 `VALIDATION_*.md`。
