"""Factory serial install for the SF32LB57GY board's CH343 reset wiring."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import time
import tempfile
import serial


def reset(port, boot):
    with serial.Serial(port=None, baudrate=1000000, timeout=.1) as uart:
        uart.rts = False
        uart.dtr = False
        uart.port = port
        uart.open()
        uart.rts = True
        uart.dtr = boot
        time.sleep(.2)
        uart.rts = False
        time.sleep(.15)
        uart.dtr = False


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--package', type=Path, default=Path(__file__).resolve().parent)
    parser.add_argument('--sftool', default=shutil.which('sftool'))
    args = parser.parse_args()
    if not args.sftool:
        parser.error('Install sftool >= 0.2.5 (must support SF32LB57) or specify --sftool')
    package = args.package.resolve()
    update = json.loads((package/'update.json').read_text(encoding='utf-8'))
    if update.get('board') != 0x05700470:
        parser.error('Wrong board package')
    images = json.loads((package/'images.json').read_text(encoding='utf-8'))
    expected = {'ER_IROM1.bin': 0x12200000, 'ER_IROM2.bin': 0x12400000,
                'epd_waveform.bin': 0x12EC0000, 'recovery.bin': 0x12020000,
                'update_state.bin': 0x12F00000, 'ftab.bin': 0x12000000, 'bootloader.bin': 0x12010000}
    if len(images) != 7 or {i['path']: int(i['address'], 0) for i in images} != expected:
        parser.error('Incomplete or unexpected factory image set')
    for image in images:
        data = (package/image['path']).read_bytes()
        if len(data) != image['size'] or hashlib.sha256(data).hexdigest() != image['sha256']:
            parser.error('Checksum mismatch: '+image['path'])
    port = args.port if args.port.upper().startswith('COM') else 'COM'+args.port
    reset(port, True)
    base = [args.sftool, '-c', 'SF32LB57', '-m', 'nor', '-p', port, '-b', '1000000',
            '--before', 'no_reset', '--after', 'no_reset']
    # A CRC-protected record can share the previous record's whole-file CRC.
    # Always invalidate both journal sectors before factory reinstallation.
    subprocess.run(base+['erase_region', '0x12F00000:0x2000'], check=True)
    reset(port, True)
    cmd = base+['write_flash', '--verify']
    cmd += [f"{package/i['path']}@{i['address']}" for i in images]
    subprocess.run(cmd, check=True)
    reset(port, True)
    with tempfile.TemporaryDirectory(prefix='reader57-journal-') as folder:
        readback = Path(folder)/'update_state.bin'
        subprocess.run(base+['read_flash', f'{readback}@0x12F00000:0x2000'], check=True)
        if readback.read_bytes() != (package/'update_state.bin').read_bytes():
            raise RuntimeError('Factory journal byte verification failed; re-run factory install')
    reset(port, False)
    print('SF32LB57 factory install verified and reset; TF contents were not formatted.')


if __name__ == '__main__':
    main()
