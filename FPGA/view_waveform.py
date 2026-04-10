#!/usr/bin/env python3
import re

def parse_vcd(filename):
    with open(filename, 'r') as f:
        content = f.read()

    # Find signal codes
    codes = {}
    for line in content.split('\n'):
        if '$var' in line:
            parts = line.split()
            name = parts[4]
            code = parts[3]
            codes[name] = code
        if '$enddefinitions' in line:
            break

    stR_code = codes.get('stR')
    stF_code = codes.get('stF')
    clk_code = codes.get('clk90')
    pushB_code = codes.get('rfPushBase')
    pushP_code = codes.get('rfPushPeak')
    pullB_code = codes.get('rfPullBase')
    pullP_code = codes.get('rfPullPeak')
    enP_code = codes.get('enP')

    # Extract transitions
    time = 0
    stR = "0"
    stF = "0"
    pushB = "0"
    pushP = "0"
    pullB = "0"
    pullP = "0"
    enP = "0"
    
    events = []
    
    lines = content.split('\n')
    for line in lines:
        if line.startswith('#'):
            time = int(line[1:])
        elif line.startswith('b') and line.endswith(' ' + stR_code):
            stR = line[1:].split()[0]
        elif line.startswith('b') and line.endswith(' ' + stF_code):
            stF = line[1:].split()[0]
        elif line.startswith('b') and line.endswith(' ' + enP_code):
            enP = line[1:].split()[0]
        elif line.endswith(pushB_code): pushB = line[0]
        elif line.endswith(pushP_code): pushP = line[0]
        elif line.endswith(pullB_code): pullB = line[0]
        elif line.endswith(pullP_code): pullP = line[0]
        elif line == '1' + clk_code:
            events.append((time, int(stR, 2), int(stF, 2), pushB, pushP, pullB, pullP, int(enP, 2)))
        
        if len(events) > 1500:
            break

    print(f"{'Time (ps)':>12} | {'stR':>3} | {'stF':>3} | {'enP status'} | {'B P b p'} | {'Wave'}")
    print("-" * 80)
    for t, r, f, pb, pp, lb, lp, ep in events:
        if r == 0 and f == 0 and t < 30000000: continue
        
        pins = f"{pb} {pp} {lb} {lp}"
        wave = ""
        for s in [r, f]:
            if s == 0 or s == 2: wave += "B" 
            elif s == 1: wave += "P"         
            elif s == 3 or s == 5: wave += "b" 
            elif s == 4: wave += "p"         
            else: wave += "."
        
        en_bit = (ep >> 15) & 1
        print(f"{t:12} | {r:3} | {f:3} | enP[15]={en_bit} | {pins} | {wave}")

if __name__ == "__main__":
    import os
    vcd_path = "waveform.vcd"
    if os.path.exists(vcd_path):
        parse_vcd(vcd_path)
    else:
        print(f"VCD not found at {vcd_path}")
