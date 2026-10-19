# SF32LB527 / E0470A03-AF-S

Board target: `sf32lb527-oed-e0470a03_hcpu`. Native panel 1216×684; reader UI uses the existing inverted-portrait rotation, 684×1216. NOR is 16MiB, PSRAM is 8MiB.

This BSP is separate from `sf32-oed-epd_v12`: the actual board has SY7636, AW9523B, FT5x46 touch and LM3630A frontlight. The pinout and timing were verified with a standalone diagnostic firmware before integration.

| Peripheral | Wiring |
|---|---|
| EPD CLK/SPH/LE/OE | PA4/PA6/PA9/PA5 |
| EPD STV/CPV/GMODE | PA0/PA1/PA2 |
| EPD D0..7 | PA7, PA8, PA37, PA39, PA40, PA41, PA42, PA43 |
| AW9523B | I2C2 PA20/PA30, address 0x59, reset PA3 |
| SY7636 | I2C1 PA10/PA11, address 0x62; EN GE6, VCOM_EN GE4, VDD33 GE14 |
| FT5x46 | I2C1 0x38; reset GE13, IRQ PA44, PA32 control |
| LM3630A | I2C2 0x36; GE3 HWEN, GE11 companion enable; A=cold, B=warm |
| TF | SPI1 MOSI PA24, MISO PA25, CLK PA28, CS PA29; GE0 active-low detect |

## Build

```powershell
git submodule update --init --recursive
.\build-sf32lb527.ps1
python tools/package_527.py --output firmware-527
```

The SDK submodule remains at the repository's pinned revision. A board-specific project profile overrides the reference-board pin/peripheral assumptions. Existing board profiles remain available.

The build produces separate main code and built-in font images under `main.bin/`, plus bootloader, Flash table and the E0470A03 waveform image. Do not use the old small diagnostic image addresses without accounting for the font/resource image.

| Image | NOR address | Partition capacity |
|---|---|---|
| `ftab.bin` | `0x12000000` | 32KiB |
| `bootloader.bin` | `0x12010000` | 64KiB |
| `ER_IROM1.bin` | `0x12200000` | 2MiB |
| `ER_IROM2.bin` | `0x12400000` | 10MiB |
| `epd_waveform.bin` | `0x12EC0000` | 256KiB |

Before first installation, read and verify the complete 16MiB NOR, not only the application partition. Keep that backup private and separate from the public source and flash bundle. The TF card is not formatted by this BSP. This image layout differs from the original application and standalone diagnostic firmware.

`package_527.py` checks the board and image sizes, then copies all five images with a self-contained sftool configuration and SHA-256 manifest into a new output directory. It does not access the serial port or flash the board. The CI artifact includes this bundle under `flash_bundle/`.

## Driver integration

- EPD uses the tested 48MHz DBI transport and gate pipeline, about 7.76ms per physical scan. Every waveform phase scans the whole panel continuously. A 3MiB row dictionary stores the complete update before high voltage is enabled; repeated rows and equivalent per-row phases share storage. The display can temporarily borrow up to 2MiB from the 3MiB EPUB/FreeType heap, leaving at least 512KiB available, and releases it after refreshing. RLE records are expanded to directly addressable rows when they fit, otherwise a full phase is expanded before scanning. There are no visible row-group passes. If a highly complex transition still exceeds available memory, the driver reports an error before enabling high voltage and retains the old display state.
- Ordinary gray updates (including antialiased text) use GL16; strictly black/white transitions can use DU. GC16 cleaning follows the UI's 5/10/20/every-update setting, with 10 as default. INIT is only used at startup or after loss of panel state. Identical framebuffers are skipped. The legacy parser's intermediate white-page flush and transient loading-hourglass updates are suppressed; a page turn submits the completed page once.
- VCOM defaults to the board-tested -1.10V and temperature to 28°C; these are not per-panel factory calibration. SY7636 shutdown retains the 150ms rail sequence and at least 450ms with EN low before re-enabling. The latter is a deadline checked on the next enable, allowing touch handling and preparation of the next page during discharge.
- Touch exposes the reader's `touch` device interface with validated packet decoding, IRQ-driven reads and primary-contact tracking. Raw portrait coordinates match the 684×1216 UI after the existing rotation mapping. Touching a UI control is dispatched once on release.
- Both settings and the reading overlay retain touch operation on this board. The overlay's left/right arrows change the cleaning interval; it does not offer a touch-off setting without a verified physical way to turn touch back on.
- LM3630A retains the tested low-level current/boost configuration and per-bank brightness cap. Only the initial ≤5% ramp can classify a one-shot OCP/OVP event as startup history; two follow-up reads must be clear. Runtime faults still shut down both enables.
- TF is mounted at `/`, matching this repository's `/fonts` and `/settings.cfg` paths. Application writes are enabled for settings and reader data; there is no automatic format. The SPI MSD source snapshot has read CRC16, bounds and transfer-length checks.
- Font/size/weight/spacing/margin settings are saved to `/settings.cfg`. Short writes, synchronization and close failures are checked; a failed save leaves the settings page open with an error. Saving is not a power-loss-atomic transaction.
- The library uses `/books` when that directory exists, otherwise `/`. Home, settings and cold/warm frontlight controls use the 684×1216 portrait dimensions. Touch hit regions account for renderer margins.
- Display refresh serializes touch, frontlight checks and diagnostic TF operations. Hardware protection remains enabled. Input during a full refresh can be delayed or coalesced.

Battery/charger routing, physical keys and deep-sleep wake routing have not been validated on this board. The BSP does not reuse old-board ADC/key assignments. Bluetooth/PAN and weather code are retained but require separate end-to-end validation with a phone/network.

## Validation status

See [hardware and build validation](VALIDATION_SF32LB527.md) for measured results and limits.

`python tools/test_527_cache.py` requires a host GCC. It replays the actual C cache routines against GC16, DU, INIT and GL16 at the configured temperature, compares every packed output byte, checks bounded overflow, and verifies the cleaning-interval policy. Hardware optical checks remain necessary for ghosting and gray quality.

`python tools/test_settings_save.py` uses host G++ to exercise the actual writer with short writes and open/write/sync/close failures. Both checks run for the new board in CI.

The touch packet implementation follows the [FocalTech FT5x06 driver protocol](https://github.com/focaltech-systems/drivers-input-touchscreen-FTS_driver/blob/master/ft5x06.c): a three-byte header followed by six-byte contact records. The detected `0x54` family ID does not distinguish all FT5x46 suffixes. The SPI MSD snapshot retains its RT-Thread copyright/license header; `src/boards/sf32lb527/upstream.json` records the source-file hashes before the board-specific changes.
