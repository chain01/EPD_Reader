"""Create a self-contained SF32LB527 flash bundle; this script never flashes."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=root / 'epdiy-epub/project/build_sf32lb527-oed-e0470a03_hcpu')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    build = args.build.resolve()
    config = (build / 'rtconfig.h').read_text(encoding='utf-8')
    if '#define BSP_USING_BOARD_SF32LB527_OED_E0470A03' not in config:
        parser.error('Build directory is not the SF32LB527 E0470A03 board')
    specs = [
        (build / 'ftab/ftab.bin', 0x12000000, 0x8000),
        (build / 'bootloader/bootloader.bin', 0x12010000, 0x10000),
        (build / 'main.bin/ER_IROM1.bin', 0x12200000, 0x200000),
        (build / 'main.bin/ER_IROM2.bin', 0x12400000, 0xA00000),
        (root / 'epdiy-epub/waveform/e0470a03/epd_waveform.bin', 0x12EC0000, 0x40000),
    ]
    images = []
    for source, address, limit in specs:
        data = source.read_bytes()
        if not 0 < len(data) <= limit:
            parser.error(f'{source.name}: image does not fit its partition')
        images.append(dict(path=source.name, address=hex(address), size=len(data),
                           sha256=hashlib.sha256(data).hexdigest()))
    args.output.mkdir(parents=True, exist_ok=False)
    for source, _, _ in specs:
        shutil.copy2(source, args.output / source.name)
    manifest = dict(chip='SF32LB52', memory='NOR', write_flash=dict(verify=True,
                    files=[dict(path=i['path'], address=i['address']) for i in images]))
    (args.output / 'sftool_param.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    (args.output / 'images.json').write_text(json.dumps(images, indent=2) + '\n', encoding='utf-8')
    (args.output / 'README.txt').write_text(
        'SF32LB527 / E0470A03-AF-S, 684x1216 portrait\n'
        'Make and verify a complete 16MiB NOR backup before first installation.\n'
        'The five images include bootloader, flash table, application, font and waveform.\n'
        'Use sftool_param.json with sftool for SF32LB52 on the verified board port.\n'
        'images.json records each address, byte length and SHA-256.\n', encoding='utf-8')
    print(f'Packaged {len(images)} images in {args.output.resolve()}')


if __name__ == '__main__':
    main()
