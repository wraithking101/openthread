#!/bin/bash
make -f examples/Makefile-cc2538 clean
make -f examples/Makefile-cc2538
arm-none-eabi-objcopy -O binary output/cc2538/bin/ot-cli-ftd output/cc2538/bin/ot-cli-ftd.bin
python ~/zolertia/cc2538-bsl/cc2538-bsl.py -e -w -v -p /dev/ttyUSB0 -a 0x00200000 output/cc2538/bin/ot-cli-ftd.bin 
