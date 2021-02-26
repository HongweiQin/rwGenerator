#!/bin/bash

outfile=$1
runningtime=$2

source ../target

set +x

fio --filename=$targetfile --output=$outfile --runtime=$runningtime tools/fiorandwrite

