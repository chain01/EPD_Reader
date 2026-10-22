"""Protected native USB updater for SF32LB57GY / E0470A03 only."""
from usb_dfu_527 import main

if __name__ == '__main__':
    main(board=0x05700470, serial_number='EPD57-RECOVERY-1')
