#!/usr/bin/python 
import sys
import re

seq = ["","","",""]
for line in sys.stdin:
    line = line.rstrip()
    m = re.search(' (fa1b1(\d)(\w+))', line)
    if m:
        seqnum = int(m.group(2))
        seq[seqnum] = m.group(1)
        if seqnum == 3:
            print(' '.join(seq))