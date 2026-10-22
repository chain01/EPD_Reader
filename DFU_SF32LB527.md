# USB 恢复升级（SF32LB527）

此板的 USB D+/D− 直接接主控。恢复程序使用 **USB CDC 虚拟串口**，配套工具为 `tools/usb_dfu_527.py`；不是 `dfu-util` 使用的 USB DFU 类协议。USB 底层复用固定 SDK 的 CherryUSB/MUSB 实现，参考 [SiFli USB-CDC 升级示例](https://docs.sifli.com/projects/sdk/latest/sf32lb52x/example/dfu_v2/cdc/app/README.html)。

## 进入与退出

1. 阅读器：功能设置 → USB 固件升级 → 进入升级。
2. 应用无法运行时：按住电源键，再接通电源或复位，保持至少 2 秒。启动程序采样 PA34 高电平持续 1.5 秒后进入恢复程序。
3. 升级中断、镜像校验失败、升级状态损坏时：下次启动自动进入恢复程序。

PA34、高有效来自原固件 `init_pin` 对 `button_config(34,1,...)` 的调用及 `KEY1 active=HIGH` 字符串；它与 PA33 的 ADC 按键组分开。具体外壳按键与手感仍以实物为准。原生 USB 引脚为 PA35(D+)/PA36(D−)。

2026-10-19：菜单进入、原生 USB 完整更新、断电中断后自动恢复均已实测；按键入口因用户暂时无法操作电源键，尚未实测。

菜单进入会先显示提示、关闭前光和屏幕高压、停用触摸，然后重启。按键或错误恢复路径不依赖屏幕/触摸/TF 驱动，屏幕可能保留旧画面；以电脑枚举的 `EPD Reader 527 Recovery` 为准。CDC 使用 SDK 示例的开发 VID/PID `38F4:0001`；产品量产前应使用有权分配的 USB 标识。

```powershell
python -m pip install pyserial
python tools/usb_dfu_527.py --info --port COM42
python tools/usb_dfu_527.py --package firmware-527 --port COM42
```

`COM42` 是示例，应选择**原生 USB 恢复端口**，不是之前调试用的 FTDI COM36。只连接一台恢复设备时可省略 `--port` 自动识别。升级成功后工具自动重启；仅想退出、且当前镜像完整时可使用 `--reboot`。一旦开始写入升级，取消或断线后需要重新传完整包。

## 日常包与首次安装

```powershell
.\build-sf32lb527.ps1
# 日常 USB 更新包，不包含恢复程序或启动程序
python tools/package_527.py --output firmware-527
# 仅空白 Flash 首刷或工厂维修：通过 SoC 下载 UART 使用 sftool
python tools/package_527.py --factory --output factory-527
```

日常包仅有应用、内置字体、波形三个目标，`update.json` 固定板型、布局、文件长度、CRC32 和 SHA-256。工具在连接设备前核验文件，并通过设备握手确认板型。没有任意地址、整片擦除、bootloader 更新或 DFU 自更新参数。

首次安装包另外包含 `recovery.bin`、`bootloader.bin`、`ftab.bin` 与初始升级状态，并生成 `flash_factory.bat` 和 `FACTORY_FIRST_FLASH.md`。空白 Flash 没有可备份的旧内容；对已有数据的设备执行工厂重装前，应先备份完整 NOR。首刷通过 SoC 下载 UART/ROM 串口下载模式完成，不能用应用启动后枚举的原生 USB CDC 恢复端口代替。脚本依次写入应用、字体、波形、恢复程序、升级状态、Flash 表，最后写入首级启动程序并校验。不要添加全片擦除参数。普通用户日常更新仍使用受保护 USB 更新包。

## 固定边界与断电恢复

| 区域 | 地址/上限 | 日常 USB 权限 |
|---|---|---|
| Flash 表 | `0x12000000` / 32KiB | 不可写 |
| 首级启动程序 | `0x12010000` / 64KiB | 不可写 |
| USB 恢复程序 | `0x12020000` / 1536KiB | 不可写 |
| 应用 | `0x12200000` / 2MiB | 固定目标 0 |
| 字体资源 | `0x12400000` / 10MiB | 固定目标 1 |
| 波形 | `0x12EC0000` / 256KiB | 固定目标 2 |
| 两份升级状态 | `0x12F00000` / 8KiB | 仅内部状态机可写 |

设备端不接受主机提供的 Flash 地址。每块数据必须符合目标编号、总长度和连续偏移，读回一致后才推进进度。写入前先持久化 `UPDATING`；三个镜像全部写完、向量及全内容 CRC 校验通过后，才追加 `READY`。两份状态记录包含序号和 CRC，写新记录时保留上一份有效记录。启动程序独立检查状态、应用向量和三个镜像 CRC，不依赖可更新的应用入口或其启动表内容。

CRC/SHA 在此用于传输与存储完整性，**不是固件签名验证**。保护保证的是这套日常 USB 升级通道不能覆盖启动/DFU：它不是 NOR 永久硬件锁，也不阻止外接 UART/SWD 下载器的全片擦除，或有完全 MCU 权限的恶意应用代码。本实现没有烧写不可逆 eFuse/OTP 保护位；不能把软件升级边界描述成抵御任意代码的硬件安全边界。

## 检查

`python tools/test_527_dfu.py` 编译实际存储核心，在模拟 NOR 中验证：28 个擦写步骤的操作前/半写入断电、重新升级、保护区不变、错误板型/越界/整数极值、未完成提交、三种镜像损坏。构建与真实 USB 枚举/更新验证需分别记录，主机测试不等于 USB 联调完成。

实物验证还包括错误目标/偏移拒绝；只传应用前 2KiB 后断电，设备自动停留在恢复模式；重传三个完整镜像并重启成功；最后独立回读首级启动程序、恢复程序和 Flash 表，SHA-256 与安装时一致。详情见 [验证记录](VALIDATION_SF32LB527.md)。
