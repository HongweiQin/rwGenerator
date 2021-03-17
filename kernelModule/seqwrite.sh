#!/bin/bash

for i in {0..4095}
do
	addrstart=`echo $i*4 | bc`
	./writedev $addrstart 4 &> /dev/null
done
