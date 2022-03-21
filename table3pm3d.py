#!/usr/bin/python3

import sys
import re

l = 0
for line in sys.stdin:
    for w,word in enumerate(line.split()):
        print("%d %d %s" % (l, w, word))
    l += 1
    print()