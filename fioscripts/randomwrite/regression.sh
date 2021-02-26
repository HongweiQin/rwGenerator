#!/bin/bash

source ../target

for i in {1..10}
do
	echo "regression="$i
	blkdiscard $targetfile
	sleep 3
	./testwriteonly.sh tmp/prepare-$i-$k

	for k in $(seq 1 $i)
	do
		echo "test="$k
		./testwriteonly.sh tmp/writeonly-$i-$k
		./testreadwrite.sh tmp/readwrite-$i-$k
	done

	echo "Regression "$i" Done"
done
exit

