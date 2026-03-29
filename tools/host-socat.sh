#!/bin/bash
sudo socat -d -d PTY,link=/dev/ttyV0,raw,echo=0,waitslave TCP:nano1.local:54321,forever,intervall=5 &
