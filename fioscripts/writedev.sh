#!/bin/bash

source ./target

startpgn=$1
nrpgs=$2

off=`echo "$startpgn * 4096" | bc`
ios=`echo "$nrpgs * 4096" | bc`

set +x

fio --filename=$targetfile --offset=$off --io_size=$ios tools/fiowritedev

