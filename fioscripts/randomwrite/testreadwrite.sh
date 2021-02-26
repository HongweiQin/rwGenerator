#!/bin/bash

source ../target

set +x

fio --filename=$targetfile --output=$1 tools/fiorandreadwrite

