# SiFli 固件烧录工具 1.0.0

Windows 10/11 x64 图形烧录工具，已内置 sftool 0.2.5，无需安装 Python。

## 使用

1. 运行 `SiFliFlashTool.exe`，选择板子的串口下载端口。
2. 点击“加载 ZIP / JSON / INI”选择完整固件 ZIP、`sftool_param.json` 或 `ImgBurnList.ini`。也可以选择固件目录自动识别清单。
3. 手动烧录时点击“添加文件”，选择文件并输入十六进制地址。地址可带 `0x` 前缀；双击条目可修改，支持多个文件及调整写入顺序。
4. 核对芯片、存储类型和文件列表，点击“开始烧录”。默认启用写后校验；导入 JSON 时沿用清单的校验设置。

ZIP 自动解压并保留目录结构，优先使用 JSON，同时存在 INI 时检查二者一致性。完整首刷包沿用清单顺序，bootloader 最后写入。支持路径中的中文及空格。

底层使用 **sftool 串口下载协议**，用于空片首刷及串口重烧。板子原生 USB 的受保护恢复模式使用另一套 USB CDC 协议，不能把该恢复端口当作 sftool 下载串口。SF32LB527 板此前调试串口为 COM34，实际端口以当前电脑为准。

默认波特率 1000000，烧录前 `default_reset`，烧录后 `soft_reset`。握手或载入程序超时时，可降低至 115200，并按硬件 RESET 后重试。高级下拉项可以选择不复位。

界面进度为 sftool 当前阶段进度，多文件烧录/校验时可能重新计数；只有进程成功退出才显示烧录完成。停止会中断当前写入，需要重新完整烧录。

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
