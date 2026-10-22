# SiFli 固件烧录工具 1.1.0

Windows 10/11 x64 图形工具，同时支持 sftool 串口烧录和 SF32LB527 阅读器的原生 USB DFU。已内置 sftool 0.2.5，无需安装 Python。

## 原生 USB 升级（组装后的阅读器）

1. 在阅读器设置中进入 USB 升级，或按住 PA33 按键后复位、上电，再松开 PA33。用数据线连接板子的原生 USB 接口。
2. 运行 `SiFliFlashTool.exe`，选择顶部“原生 USB DFU”。工具每 1.5 秒自动识别恢复设备，只有一个时自动选中，无需调试串口。
3. 加载完整固件 ZIP、固件目录、`sftool_param.json`、`ImgBurnList.ini` 或 `update.json`。完整首刷包会自动筛出应用、字体和波形三个镜像，不写入引导和恢复程序。
4. 点击“开始 USB 升级”。依次写入三个镜像，设备端读回校验全部通过后自动请求重启阅读器。

“读取设备”仅查询恢复程序信息；“重启退出升级”会由设备先检查现有固件，再请求返回阅读器。也可在 PA33 松开时长按电源键约 10 秒硬件重启。若升级中断导致固件不完整，设备会保留恢复模式，需重新完整升级后才能返回阅读器。

USB 模式只识别 VID `38F4` / PID `0001` / 序列号 `EPD527-RECOVERY-1` 的现有 SF32LB527 阅读器恢复固件。它使用项目自己的 USB CDC 恢复协议，不依赖 sftool 或 USB 转串口芯片，也不是通用 USB DFU Class 工具。应用尚未进入恢复模式时，上位机不能通过 USB 命令强制它进入；请使用设置菜单或 PA33 入口。

USB 模式也支持手动选择文件和地址，但必须提供当前布局的完整三个镜像：应用 `0x12200000`、字体 `0x12400000`、波形 `0x12EC0000`。工具先检查分区长度、应用向量；含升级元数据时检查板型、布局、SHA-256 和 CRC。USB 地址不可用于写其他分区，设备端协议同样不接受任意地址。

USB 进度 0–90% 为按总字节数统计的写入进度，随后执行设备端校验与重启。拔线、超时、停止或校验失败均不会自动启动不完整固件；重新进入恢复模式后从头升级，不支持断点续传。

## 串口烧录（空片首刷 / 工厂维护）

1. 运行 `SiFliFlashTool.exe`，选择“串口烧录”和板子的串口下载端口。
2. 点击“加载 ZIP / JSON / INI”选择完整固件 ZIP、`sftool_param.json` 或 `ImgBurnList.ini`。也可以选择固件目录自动识别清单。
3. 手动烧录时点击“添加文件”，选择文件并输入十六进制地址。地址可带 `0x` 前缀；双击条目可修改，支持多个文件及调整写入顺序。
4. 核对芯片、存储类型和文件列表，点击“开始烧录”。默认启用写后校验；导入 JSON 时沿用清单的校验设置。

ZIP 自动解压并保留目录结构，优先使用 JSON，同时存在 INI 时检查二者一致性。完整首刷包沿用清单顺序，bootloader 最后写入。支持路径中的中文及空格。

串口模式使用 **sftool 串口下载协议**，用于空片首刷及串口重烧，可写完整引导和恢复分区。原生 USB 恢复端口应使用 USB DFU 模式。SF32LB527 板此前调试串口为 COM34，实际端口以当前电脑为准。

默认波特率 1000000，烧录前 `default_reset`，烧录后 `soft_reset`。握手或载入程序超时时，可降低至 115200，并按硬件 RESET 后重试。高级下拉项可以选择不复位。

串口界面进度为 sftool 当前阶段进度，多文件烧录/校验时可能重新计数；只有进程成功退出才显示烧录完成。停止会中断当前写入，需要重新完整烧录。

运行日志自动写入 `%LOCALAPPDATA%\SiFliFlashTool\logs`，也可点击保存日志。导入时检查文件存在、非空、地址范围、重叠；包内含 `images.json` 时同时检查匹配项的长度、地址和 SHA-256。

## 从源码运行和构建

需要 Python 3.11+、`pyserial`、PyInstaller，Tkinter 使用 Python Windows 安装包自带组件。

```powershell
python -m pip install pyserial pyinstaller
python tools/flash_gui/flash_gui.py
python -m unittest discover -s tools/flash_gui -p 'test_*.py' -v
./tools/flash_gui/build.ps1 -OutputDirectory C:/output/SiFliFlashTool
```

`build.ps1` 默认使用用户目录 `.sifli/tools/sftool/0.2.5/sftool.exe`；可通过 `-Sftool` 指定其他工具文件。包含的 sftool 为独立上游工具，其许可证见 https://github.com/OpenSiFli/sftool 。

USB 帧、CRC、序号和板型校验直接复用 `tools/usb_dfu_527.py`，GUI 后端增加可中止读取和总进度。发布包 `source/` 包含这份共享模块；单独复制源码运行时请保持它与 `usb_core.py` 同目录，或保留仓库的目录结构。
