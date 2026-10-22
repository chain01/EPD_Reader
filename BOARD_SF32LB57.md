# SF32LB57GY / E0470A03-AF-S 阅读器

目标板 `sf32lb57gy-oed-e0470a03_hcpu`，SF32LB57GYD7N6，16MB NOR、16MB PSRAM。
屏幕原生 1216×684，阅读器使用竖屏 684×1216。

## 接线和驱动

| 功能 | 硬件与引脚 |
|---|---|
| 显示 | 原生 LCDC/TCON，数据 PA28–33/37/38，控制 PA24–27/39–41 |
| 屏幕电源 | TPS65185，I²C1 PA20/21，0x68；WAKE PA42、VCOM PA43、PWRUP PA44、PGOOD PA52 |
| 触摸 | FT5446，I²C2 PA1/2，0x38；IRQ PA3、RESET PA4、供电 PA5 |
| 前光 | AW99703，I²C1/0x36，HWEN PA53；CH2 冷光、CH3 暖光 |
| TF | SDMMC2，PA45/46/47/48/49/50；PA51 低电平表示有卡 |
| 待机唤醒键 | PA34，高电平有效 |
| 调试串口 | UART1，PA18 RX / PA19 TX，1000000 baud |
| 原生 USB | PA35 D+ / PA36 D−；COM41 是 CH343 调试/下载串口 |

TPS 使用本板此前验证的 VCOM=-2.10V 与 22V 电源配置。
前光芯片所有通道共用亮度寄存器，因此界面提供统一亮度与冷光/混光/暖光；
最大电流维持已验证的每串 4.8mA，保留过压、过流、热保护和故障自动关灯。

TF 接口始终有 3.3V，不能像 527 板那样单独断电。驱动在请求间关闭 SD 时钟，
保留挂载与文件句柄；卡自身的待机电流取决于卡型号。不会格式化现有 TF 卡。
本目标是离线阅读器，蓝牙、Wi-Fi、联网天气服务未启用。

## 编译

仓库自带的 SiFli-SDK 子模块及其锁定工具链：

```powershell
.\build-sf32lb57gy.ps1
python tools/package_57.py --factory --output firmware-57-factory
```

日常 USB 升级包去掉 `--factory`。两个板型分别编译、分别打包，不能混刷。
57 板 ID 为 `0x05700470`，527 板为 `0x05270470`。

## 空设备串口首次安装

需要 Python、`pip install pyserial`、支持 SF32LB57 的 sftool（0.2.5 或更新版本）。
解压后完整保留所有文件，在包目录运行：

```powershell
python flash_factory_57.py --port COM41 --sftool C:\path\to\sftool.exe
```

也可在 sftool 已加入 PATH 后运行 `flash_factory.bat COM41`。
脚本按这块板 CH343 的 RTS/DTR 接法进入 ROM 下载，校验所有包文件，再写入并验证七个分区。
升级状态区会先单独擦除，写后再逐字节读回；避免 CRC 相同而被下载工具跳过旧状态块。
不要在写入期间断电。其他板型必须使用其对应固件包。

`sftool_param.json` 为 SDK sftool 原生格式，`ImgBurnList.ini` 为下载工具镜像列表，
均使用包内相对路径。使用图形工具时选择 SF32LB57 / NOR 并启用写后校验。
若工具无法自动复位，可用本包脚本；它显式处理 CH343 的复位和 BOOT 时序。
重复工厂重刷时，建议先单独擦除 `0x12F00000:0x2000`，配套脚本已自动执行。

| 地址 | 镜像 |
|---|---|
| 0x12000000 | ftab.bin |
| 0x12010000 | bootloader.bin |
| 0x12020000 | recovery.bin |
| 0x12200000 | ER_IROM1.bin，应用 |
| 0x12400000 | ER_IROM2.bin，内置字体 |
| 0x12EC0000 | epd_waveform.bin |
| 0x12F00000 | update_state.bin |

## 阅读与待机

EPUB 放 TF 根目录，额外矢量字体放 `/fonts`。功能设置提供前光、阅读字体与字号、
全刷间隔、10/15/20 秒自动翻页、时钟待机。
连续正文翻页使用 GL16，按设置周期 GC16 全刷。主页、设置、书库、阅读菜单等界面
使用 GC16 清屏；从这些界面返回正文也先清屏一次，避免大幅更换版面留下旧菜单轮廓。
时钟每分钟更新，10 次局刷后全刷。
时钟待机保留 SRAM+PSRAM，关闭触摸、前光和屏幕高压，PA34 唤醒恢复原画面。
未使用的功放、麦克风与加速度计保持关闭。实际电流需在电池端拔掉 USB/串口后测量。

## 受保护的 USB 升级

首次串口安装后，从功能设置进入 USB 固件升级，或开机时按住 PA34 约 2 秒。
原生 USB 会枚举为 `EPD57-RECOVERY-1`（与 CH343 的 COM41 不同）：

```powershell
python usb_dfu_57.py --package . --port COMxx
```

升级协议仅允许写应用、字体、波形和升级状态，不提供写启动/恢复程序的命令。
未提交或校验失败的应用不能启动，复位后留在恢复模式。此限制属于软件分区保护，
串口工厂重刷、外部编程器或另有权限的应用仍可改写 NOR；不是 OTP/永久写保护。
原生 USB 枚举和 PA34 入口是否已在当前接线下实测，见验证记录。
