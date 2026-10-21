"""Package a protected USB update, or an explicit first-install factory bundle."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import zlib
from usb_dfu_527 import BOARD,load_package


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=root / 'epdiy-epub/project/build_sf32lb527-oed-e0470a03_hcpu')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--factory',action='store_true',help='Include boot/DFU installation images; never use for routine USB updates')
    args = parser.parse_args()
    build = args.build.resolve()
    config = (build / 'rtconfig.h').read_text(encoding='utf-8')
    if '#define BSP_USING_BOARD_SF32LB527_OED_E0470A03' not in config:
        parser.error('Build directory is not the SF32LB527 E0470A03 board')
    specs = [
        (build / 'main.bin/ER_IROM1.bin', 0x12200000, 0x200000),
        (build / 'main.bin/ER_IROM2.bin', 0x12400000, 0xA00000),
        (root / 'epdiy-epub/waveform/e0470a03/epd_waveform.bin', 0x12EC0000, 0x40000),
    ]
    if args.factory:
        specs += [(build/'recovery/recovery.bin',0x12020000,0x180000),
                  (build/'ftab/ftab.bin',0x12000000,0x8000),
                  (build/'bootloader/bootloader.bin',0x12010000,0x10000)]
    images = []
    for source, address, limit in specs:
        data = source.read_bytes()
        if not 0 < len(data) <= limit:
            parser.error(f'{source.name}: image does not fit its partition')
        images.append(dict(path=source.name, address=hex(address), size=len(data),
                           sha256=hashlib.sha256(data).hexdigest(),crc32=zlib.crc32(data)))
    args.output.mkdir(parents=True, exist_ok=False)
    for source, _, _ in specs:
        shutil.copy2(source, args.output / source.name)
    update=dict(board=BOARD,layout=1,protocol=1,images=[dict(id=n,file=i['path'],size=i['size'],
        sha256=i['sha256'],crc32=i['crc32']) for n,i in enumerate(images[:3])])
    (args.output/'update.json').write_text(json.dumps(update,indent=2)+'\n',encoding='utf-8')
    load_package(args.output) # Validate vectors, hashes and board before publishing.
    if args.factory:
        words=[0x52444655,1,1,0x52454144,BOARD,1,3]
        for i in images[:3]:words += [i['size'],i['crc32']]
        record=struct.pack('<13I',*words);record+=struct.pack('<I',zlib.crc32(record))
        metadata=record+b'\xff'*(8192-len(record))
        (args.output/'update_state.bin').write_bytes(metadata)
        images.insert(4,dict(path='update_state.bin',address='0x12f00000',size=len(metadata),
            sha256=hashlib.sha256(metadata).hexdigest(),crc32=zlib.crc32(metadata)))
        manifest = dict(chip='SF32LB52', memory='NOR', write_flash=dict(verify=True,
                        files=[dict(path=i['path'], address=i['address']) for i in images]))
        (args.output / 'sftool_factory.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    (args.output / 'images.json').write_text(json.dumps(images, indent=2) + '\n', encoding='utf-8')
    (args.output / 'README.txt').write_text(
        'SF32LB527 / E0470A03-AF-S, 684x1216 portrait\n'
        'Make and verify a complete 16MiB NOR backup before first installation.\n'
        'Routine update: enter USB upgrade from Settings, or hold the PA34 power key at boot.\n'
        'Run: python usb_dfu_527.py --package <this-directory> --port <native-USB-COM-port>\n'
        'update.json permits only application, font and waveform images.\n'
        'The recovery protocol cannot write its own loader, bootloader or Flash table.\n'
        + ('FACTORY INSTALLATION ONLY: sftool_factory.json also installs boot/recovery and initial state.\n'
           'It must only be used with a verified full backup and serial/debug recovery access.\n' if args.factory else '')
        + 'images.json records byte lengths and SHA-256. See DFU_SF32LB527.md for recovery/protection limits.\n', encoding='utf-8')
    shutil.copy2(root/'tools/usb_dfu_527.py',args.output/'usb_dfu_527.py')
    for name in ('DFU_SF32LB527.md','BOARD_SF32LB527.md','VALIDATION_SF32LB527.md','VALIDATION_PREFETCH_527.md','VALIDATION_WAVEPREP_527.md','VALIDATION_FONT_POWER_527.md','VALIDATION_POWER_DIAG_527.md','MINIMUM_POWER_527.md','POWER_STEPS_527.md','TF_POWER_527.md','AUTO_TURN_527.md','STANDBY_CLOCK_527.md'):
        shutil.copy2(root/name,args.output/name)
    print(f'Packaged {len(images)} images in {args.output.resolve()}')


if __name__ == '__main__':
    main()
