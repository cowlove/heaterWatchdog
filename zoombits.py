#!/usr/bin/python3

import sys
import re

for line in sys.stdin:
    for i, c in enumerate(line):
        try: 
            print(format(int(c, 16), "08b"),end='')
            0
        except:
            print(c, end='')

