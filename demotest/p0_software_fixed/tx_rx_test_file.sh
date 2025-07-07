#!/bin/sh

user/cli/sample/avmm_rx  blksize=819200 qdeep=1024 count=1024 out=out.bin  &
sleep 1
user/cli/sample/avmm_tx  blksize=819200 qdeep=1024 count=1024 in=random.bin  &

# you can use "pkill avmm" to kill all bagrounded running test


