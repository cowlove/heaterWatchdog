#!/usr/bin/python3

import sys
import re

lastline = ""
for line in sys.stdin:
    if len(lastline) > 0:
        for i, c in enumerate(line):
            if i < len(lastline) and line[i] != lastline[i]:
                print("1 ", end='')
            else:
                print("0 ", end='')
        print()
    lastline = line
    