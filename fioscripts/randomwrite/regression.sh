#!/bin/bash

source ../target

for i in {1..10}
do
	echo "regression="$i
	blkdiscard $targetfile
	sleep 3
	echo "Prepare:"
	./testwriteonly.sh tmp/prepare-$i-$k 120
	echo ""

	runningtime=60
	for k in $(seq 1 $i)
	do
		echo "test="$k""
		./testwriteonly.sh tmp/writeonly-$i-$k $runningtime
		echo ""
		./testreadwrite.sh tmp/readwrite-$i-$k $runningtime
		echo ""
		runningtime=`echo $runningtime + 60 | bc`
	done

	echo "Regression "$i" Done"
done
exit

