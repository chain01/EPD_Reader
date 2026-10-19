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
```

The SDK submodule remains at the repository's pinned revision. A board-specific project profile overrides the reference-board pin/peripheral assumptions. Existing board profiles remain available.

The build produces separate main code and built-in font images under `main.bin/`, plus bootloader, Flash table and the E0470A03 waveform image. Do not use the old small diagnostic image addresses without accounting for the font/resource image.

## Driver integration

- EPD uses the tested 48MHz DBI transport and gate pipeline. A 2MiB cache contains all waveform phases for a bounded group of unique rows; larger images use additional neutral-masked passes. This preserves row drive duration while reserving 4MiB for EPUB/FreeType allocations. VCOM defaults to the board-tested -1.10V and temperature to 28°C; these are not per-panel factory calibration.
- Touch exposes the reader's `touch` device interface with validated packet decoding, IRQ-driven reads and primary-contact tracking. Raw portrait coordinates match the 684×1216 UI after the existing rotation mapping. Touching a UI control is dispatched once on release.
- LM3630A retains the tested low-level current/boost configuration and per-bank brightness cap. Only the initial ≤5% ramp can classify a one-shot OCP/OVP event as startup history; two follow-up reads must be clear. Runtime faults still shut down both enables.
- TF is mounted at `/`, matching this repository's `/fonts` and `/settings.cfg` paths. Application writes are enabled for settings and reader data; there is no automatic format. The SPI MSD source snapshot has read CRC16, bounds and transfer-length checks.
- Display refresh serializes touch, frontlight checks and diagnostic TF operations. Hardware protection remains enabled. Input during a full refresh can be delayed or coalesced.

Battery/charger routing, physical keys and deep-sleep wake routing have not been validated on this board. The BSP does not reuse old-board ADC/key assignments. Bluetooth/PAN and weather code are retained but require separate end-to-end validation with a phone/network.

## Validation status

2026-10-19: board integration compiles with the pinned SDK, including LCPU and Bluetooth/PAN. Reader/UI hardware validation is in progress. See the final validation record added alongside the completed integration; standalone diagnostic results alone are not evidence that every reader function works.
