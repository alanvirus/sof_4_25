#!/bin/bash 
#input /home/fpga/testDir/demotest/p0_software_fixed
device=$1
#device=0000:01:00.0
bar=2
#test_clear
#../iic_reg_rw  bar=$bar device=$device fpga write addr=0x48 value=0x0
#prepare
../iic_reg_rw  bar=$bar device=$device fpga write addr=0x50 value=0x1
#enable
../iic_reg_rw  bar=$bar device=$device fpga write addr=0x58 value=0x1
#finish
rxDone=$(eval ../iic_reg_rw  bar=$bar device=$device fpga read addr=0x60 | awk '{printf $5}')
while ! [ $rxDone == "0x00000000" ];
do
  rxDone=$(eval ../iic_reg_rw  bar=$bar device=$device fpga read addr=0x60 | awk '{printf $5}')
done
let txnum=8*$(eval ../iic_reg_rw  bar=$bar device=$device fpga read addr=0x68 | awk '{printf $5}')
let rxnum=$(eval ../iic_reg_rw  bar=$bar device=$device fpga read addr=0x70 | awk '{printf $5}')

echo "rxnum: "$rxnum" ; txnum: "$txnum
echo "errnum: "$(eval ../iic_reg_rw  bar=$bar device=$device fpga read addr=0x78 | awk '{printf $5}')