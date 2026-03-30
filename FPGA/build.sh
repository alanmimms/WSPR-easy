#!/bin/bash
set -e

# Configuration
PROJECT=wspr_ease
TOP=Top
SOURCES="top.sv sequencer.sv"
PCF=pins.pcf
DEVICE=up5k
PACKAGE=sg48

echo "--- Building FPGA Bitstream ---"

# 1. Synthesis
echo "Running Yosys..."
yosys -p "read_verilog -sv $SOURCES; synth_ice40 -top $TOP -json $PROJECT.json"

# 2. Place and Route
echo "Running NextPNR..."
nextpnr-ice40 --$DEVICE --package $PACKAGE --json $PROJECT.json --pcf $PCF --asc $PROJECT.asc

# 3. Bitstream Generation
echo "Running Icepack..."
icepack $PROJECT.asc $PROJECT.bin

echo "--- Build Complete: $PROJECT.bin ---"
mv $PROJECT.bin fpga.img
echo "Final Image: fpga.img"
