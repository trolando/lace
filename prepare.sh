#!/bin/bash
./lace.sh $1 > lace-$1.h
sed "s:lace\.h:lace-$1\.h:g" lace.c > lace-$1.c
