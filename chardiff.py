#!/usr/bin/python3

import sys
import re

lastline = ""
changemask = []
for line in sys.stdin:
    for i, c in enumerate(line):
        if i < len(changemask) and changemask[i] == True:
            print("\033[33m", end='')
        if i < len(lastline) and line[i] != lastline[i]:
            if (i >= len(changemask)):
                changemask.extend([False] * (i - len(changemask) + 1))
            changemask[i] = True
            print("\033[31m", end='')
        print (line[i], end='')
        print ("\033[0m", end='')
    lastline = line