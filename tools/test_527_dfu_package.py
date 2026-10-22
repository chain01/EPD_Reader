"""Check packaged image bounds and host protocol encoding without USB hardware."""
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import zlib
from usb_dfu_527 import BOARD,NAMES,encode,load_package
from package_527 import factory_state

def check_board(board):
    with tempfile.TemporaryDirectory(prefix='reader527-update-') as directory:
        root=Path(directory)
        arrays=[struct.pack('<II',0x20002000,0x12200009)+bytes(range(128)),b'font-data'*20,b'wave-data'*20]
        meta=dict(board=board,layout=1,protocol=1,images=[])
        for i,(name,data) in enumerate(zip(NAMES,arrays)):
            (root/name).write_bytes(data)
            meta['images'].append(dict(id=i,file=name,size=len(data),sha256=hashlib.sha256(data).hexdigest(),crc32=zlib.crc32(data)))
        def save(): (root/'update.json').write_text(json.dumps(meta))
        def rejected():
            try:load_package(root,board)
            except (ValueError,KeyError):return
            raise AssertionError('Invalid package accepted')
        save();images,manifest=load_package(root,board);assert images==arrays
        assert struct.unpack_from('<I',manifest)[0]==board
        first=factory_state(meta)
        assert zlib.crc32(first[:52])==struct.unpack_from('<I',first,52)[0]
        meta['images'][0]['crc32']^=1
        second=factory_state(meta)
        assert first!=second and zlib.crc32(first)!=zlib.crc32(second)
        meta['images'][0]['crc32']^=1
        frame=encode(2,9,manifest);assert len(frame)==56
        assert struct.unpack_from('<I',frame,16)[0]==zlib.crc32(frame[20:],zlib.crc32(frame[:16]))
        meta['board']=0x05700470 if board==BOARD else BOARD;save();rejected();meta['board']=board
        meta['images'][0]['file']='bootloader.bin';save();rejected();meta['images'][0]['file']=NAMES[0]
        meta['images'][0]['id']=0x12000000;save();rejected();meta['images'][0]['id']=0
        meta['images'][0]['size']=0xffffffff;save();rejected();meta['images'][0]['size']=len(arrays[0]);save()
        (root/NAMES[1]).write_bytes(b'wrong data');rejected()
    print(f'DFU_PACKAGE_TARGETS_HASHES_AND_WIRE_PASS board={board:08x}')

def main():
    for board in (BOARD,0x05700470):check_board(board)

if __name__=='__main__':main()
