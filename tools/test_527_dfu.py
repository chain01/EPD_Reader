"""Build and run the protected DFU core against simulated NOR and power cuts."""
from pathlib import Path
import argparse
import subprocess
import tempfile
parser=argparse.ArgumentParser()
parser.add_argument('--gcc',default='gcc')
parser.add_argument('--board57',action='store_true')
args=parser.parse_args()
root=Path(__file__).resolve().parents[1]
common=root/'epdiy-epub/dfu527/common'
with tempfile.TemporaryDirectory(prefix='reader527-dfu-') as directory:
    exe=Path(directory)/'test.exe'
    flags=['-DBSP_USING_BOARD_SF32LB57GY_OED_E0470A03'] if args.board57 else []
    subprocess.run([args.gcc,'-std=c99','-O2','-Wall','-Wextra','-Werror']+flags+[
        '-I'+str(common),str(common/'reader_dfu.c'),str(root/'tools/test_527_dfu.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
