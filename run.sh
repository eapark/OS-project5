#!/bin/sh

NPAGES=100
#NFRAMES="10"
NFRAMES="3 10 20 50 70 100"
PROGRAM="scan sort focus"
POLICY="rand fifo custom"
#PROGRAM="sort"
#POLICY="custom"
TRIALS=5

for pro in $PROGRAM; do
	for po in $POLICY; do
		for f in $NFRAMES; do
			NREAD_SUM=0
			NWRITE_SUM=0
			NFAULT_SUM=0
			for i in $(seq 1 $TRIALS); do
				RESULT=`./virtmem $NPAGES $f $po $pro`
				NFAULT_SUM=`expr $NFAULT_SUM + $(echo $RESULT | cut -d ' ' -f 2)`
				NREAD_SUM=`expr $NREAD_SUM + $(echo $RESULT | cut -d ' ' -f 4)`
				NWRITE_SUM=`expr $NWRITE_SUM + $(echo $RESULT | cut -d ' ' -f 6)`
			done
			NFAULT_AVG=`expr $NFAULT_SUM / $TRIALS`
			NREAD_AVG=`expr $NREAD_SUM / $TRIALS`
			NWRITE_AVG=`expr $NWRITE_SUM / $TRIALS`
			echo "./virtmem "$NPAGES $f $po $pro $NFAULT_AVG $NREAD_AVG $NWRITE_AVG
		done
	done
done
