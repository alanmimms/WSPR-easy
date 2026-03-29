#!/bin/bash
socat -d -d TCP-LISTEN:54321,reuseaddr,fork /dev/ttyACM0,raw,echo=0,nonblock,b921600

