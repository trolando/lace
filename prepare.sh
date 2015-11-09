#!/bin/bash
P=$1
TARGETDIR=$2
if test -z $TARGETDIR; then TARGETDIR="."; fi
./lace.sh $P > $TARGETDIR/lace-$P.h
sed "s:lace\.h:lace-$P\.h:g" lace.c > $TARGETDIR/lace-$P.c
