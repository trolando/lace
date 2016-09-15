#!/bin/bash
TARGETDIR=$1
if test -z $TARGETDIR; then TARGETDIR="."; fi
./lace.sh 6 > lace.h
./lace.sh 6 > $TARGETDIR/lace.h
./lace.sh 7 > $TARGETDIR/lace-7.h
./lace.sh 8 > $TARGETDIR/lace-8.h
sed "s:lace\.h:lace-7\.h:g" lace.c > $TARGETDIR/lace-7.c
sed "s:lace\.h:lace-8\.h:g" lace.c > $TARGETDIR/lace-8.c
