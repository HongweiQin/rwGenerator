#!/bin/bash

outfile=$1

source ../target

set +x

fio --filename=$targetfile --output=$outfile ../tools/fioverifydev
