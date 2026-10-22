"""Compile the 57 wrapper on host and compare every drive code to the vendor BIN."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import tempfile

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--gcc',default='gcc')
args=parser.parse_args()
root=Path(__file__).resolve().parents[1]
panel=root/'epdiy-epub/src/boards/sf32lb57/panel'
wave=root/'epdiy-epub/waveform/e0470a03'
with tempfile.TemporaryDirectory(prefix='reader57-wave-') as directory:
    temp=Path(directory)
    (temp/'rtthread.h').write_text('#include <stdint.h>\n#include <stddef.h>\n#define RT_ERROR 1\n#define RT_EINVAL 10\n')
    (temp/'mem_map.h').write_text('#define CUSTOM_EPD_WAVE_TABLE_SIZE 262144\n')
    exe=temp/'test.exe'
    subprocess.run([args.gcc,'-std=c99','-O2','-Wall','-Wextra','-Werror',
                    '-I'+str(temp),'-I'+str(panel),'-I'+str(wave),
                    str(panel/'epd_waveform.c'),str(root/'tools/test_57_waveform.c'),
                    '-o',str(exe)],check=True)
    dump=temp/'text-lut.bin'
    subprocess.run([str(exe),str(wave/'epd_waveform.bin'),str(dump)],check=True)
    data=dump.read_bytes()
    digest=hashlib.sha256(data).hexdigest()
    assert len(data)==44*256
    assert digest=='5ccda4fb7a12f88db29124290cf180519302e60c3d0a33ff9556222b6883519c', 'Validated R23 LUT changed'
    print('TEXT_LUT_SHA256='+digest)
