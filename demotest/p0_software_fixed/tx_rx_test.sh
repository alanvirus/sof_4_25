#!/bin/sh

user/cli/sample/avmm_rx  blksize=1024 qdeep=32 count=1 -v &
sleep 1
user/cli/sample/avmm_tx  blksize=1024 qdeep=32 count=1 -v &

# you can use "pkill avmm" to kill all bagrounded running test


