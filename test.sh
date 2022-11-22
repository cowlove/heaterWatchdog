#!/bin/bash 
touch testexpected.out
PROG=./csim_`basename $(pwd)`
OUT=testresult.out
EXPECTED=testexpected.out

#rm $PROG

make $PROG > /dev/null && $PROG --seconds 600  | tail -1 > $OUT &&\
tail -1 $OUT | diff - $EXPECTED


