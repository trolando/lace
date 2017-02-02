#!/bin/bash
TARGETDIR=$1
if test -z $TARGETDIR; then TARGETDIR="."; fi
./lace.sh 6 > $TARGETDIR/lace.h
./lace.sh 14 > $TARGETDIR/lace14.h
sed "s:lace\.h:lace14\.h:g" lace.c > $TARGETDIR/lace14.c
