#!/bin/sh

user/cli/simple_app/sapp -b 0000:01:00.0 -r -p 819200 -s 838860800 &
sleep 1
user/cli/simple_app/sapp -b 0000:01:00.0 -t -p 819200 -s 838860800 &


