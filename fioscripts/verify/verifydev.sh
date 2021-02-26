#!/bin/bash

source ../target

set +x

fio --filename=$targetfile ../tools/fioverifydev
