#!/bin/bash
TARGETDIR=$1
if test -z "$TARGETDIR"; then TARGETDIR="."; fi
mkdir -p $TARGETDIR/include
src/lace.sh 6 | gawk --load readfile '{if ($1=="LACE_CONFIG_HERE") {printf readfile("'$TARGETDIR'/lace_config.h")} else print}' > "$TARGETDIR/include/lace.h"
src/lace.sh 14 | gawk --load readfile '{if ($1=="LACE_CONFIG_HERE") {printf readfile("'$TARGETDIR'/lace_config.h")} else print}' > "$TARGETDIR/include/lace14.h"
sed "s:lace\.h:lace14\.h:g" src/lace.c > "$TARGETDIR/lace14.c"
