#!/bin/bash

for i in {1..10}
do
	echo "i="$i
	blkdiscard /dev/lrksdev
	rm local-wv-* -f
	sleep 3

	for k in $(seq 1 $i)
	do
		echo "k="$k
		./verifydev.sh &> tmp/out-$i-$k
	done

	echo "Regression "$i" Done"
done
exit

