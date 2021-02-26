#!/bin/bash

source ../target

for i in {1..10}
do
	echo "Regression="$i
	blkdiscard $targetfile
	rm local-wv-* -f
	sleep 3

	for k in $(seq 1 $i)
	do
		echo "test="$k
		./verifydev.sh tmp/out-$i-$k
		echo ""
	done

	echo "Regression "$i" Done"
done
exit

