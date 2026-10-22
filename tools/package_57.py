"""Package the SF32LB57GY E0470A03 build; never accept a 527 build."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import zlib
from usb_dfu_527 import load_package
from package_527 import factory_state

BOARD = 0x05700470


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=root / 'epdiy-epub/project/build_sf32lb57gy-oed-e0470a03_hcpu')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--factory', action='store_true')
    args = parser.parse_args()
    build = args.build.resolve()
    if '#define BSP_USING_BOARD_SF32LB57GY_OED_E0470A03' not in (build / 'rtconfig.h').read_text(encoding='utf-8'):
        parser.error('Not an SF32LB57GY E0470A03 build')
    specs = [
        (build / 'main.bin/ER_IROM1.bin', 0x12200000, 0x200000),
        (build / 'main.bin/ER_IROM2.bin', 0x12400000, 0xA00000),
        (root / 'epdiy-epub/waveform/e0470a03/epd_waveform.bin', 0x12EC0000, 0x40000),
    ]
    if args.factory:
        specs += [(build/'recovery/recovery.bin', 0x12020000, 0x180000),
                  (build/'ftab/ftab.bin', 0x12000000, 0x8000),
                  (build/'bootloader/bootloader.bin', 0x12010000, 0x10000)]
    images = []
    for source, address, limit in specs:
        data = source.read_bytes()
        if not 0 < len(data) <= limit:
            parser.error(f'{source}: image does not fit')
        images.append(dict(path=source.name, address=hex(address), size=len(data),
                           sha256=hashlib.sha256(data).hexdigest(), crc32=zlib.crc32(data)))
    args.output.mkdir(parents=True, exist_ok=False)
    for source, _, _ in specs:
        shutil.copy2(source, args.output/source.name)
    update = dict(board=BOARD, layout=1, protocol=1, images=[dict(id=n, file=i['path'], size=i['size'],
        sha256=i['sha256'], crc32=i['crc32']) for n, i in enumerate(images[:3])])
    (args.output/'update.json').write_text(json.dumps(update, indent=2)+'\n', encoding='utf-8')
    load_package(args.output, BOARD)
    if args.factory:
        data = factory_state(update)
        (args.output/'update_state.bin').write_bytes(data)
        images.insert(4, dict(path='update_state.bin', address='0x12f00000', size=len(data),
                             sha256=hashlib.sha256(data).hexdigest(), crc32=zlib.crc32(data)))
        manifest = dict(chip='SF32LB57', memory='NOR', write_flash=dict(verify=True,
                        files=[dict(path=i['path'], address=i['address']) for i in images]))
        (args.output/'sftool_param.json').write_text(json.dumps(manifest, indent=2)+'\n', encoding='utf-8')
        lines = ['[FILEINFO]']
        for n, image in enumerate(images):
            lines += [f"FILE{n}={image['path']}", f"ADDR{n}={image['address']}"]
        lines += [f'NUM={len(images)}']
        (args.output/'ImgBurnList.ini').write_text('\n'.join(lines)+'\n', encoding='utf-8')
    (args.output/'images.json').write_text(json.dumps(images, indent=2)+'\n', encoding='utf-8')
    scripts = ['usb_dfu_527.py','usb_dfu_57.py']
    if args.factory:scripts.append('flash_factory_57.py')
    for name in scripts:
        shutil.copy2(root/'tools'/name, args.output/name)
    shutil.copy2(root/'BOARD_SF32LB57.md', args.output/'README.md')
    if (root/'VALIDATION_SF32LB57.md').exists():
        shutil.copy2(root/'VALIDATION_SF32LB57.md', args.output/'VALIDATION.md')
    (args.output/'PACKAGE_KIND.txt').write_text(
        'SF32LB57GY ONLY. Factory serial first install: flash_factory.bat COM41\n' if args.factory else
        'SF32LB57GY ONLY. Application USB update: python usb_dfu_57.py --package . --port COMxx\n',encoding='utf-8')
    if args.factory:
        (args.output/'flash_factory.bat').write_text('@echo off\ncd /d "%~dp0"\npython flash_factory_57.py --port %1\n', encoding='utf-8')
    print(f'Packaged {len(images)} images in {args.output.resolve()}')


if __name__ == '__main__':
    main()
