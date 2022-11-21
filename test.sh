#!/bin/bash 
touch testexpected.out
PROG=./`basename $(pwd)`_csim
OUT=testresult.out
EXPECTED=testexpected.out

#rm $PROG

make $PROG && $PROG --seconds 600  | tail -1 > $OUT &&\
tail -1 $OUT | diff - $EXPECTED


