#!/bin/bash
#
# control-preferchers.sh

# Permission check
if [ $(id -u) != 0 ]
then
  echo "Root permission is required to run this script!"
  exit 1
fi

usage() 
{
	echo "usage: $0 -d or -e"
	echo "-d: disable prfetchers"
	echo "-e: enable preferchers" 
	exit 1
}


if [ $# -ne 1 ]
then
	usage
fi

# check msr module
lsmod | grep msr >& /dev/null
if [ $? -ne 0 ]
then
	echo "modprobe msr"
	modprobe msr
fi

num_cpus=`grep ^processor /proc/cpuinfo | sort -u | wc -l`
echo "$num_cpus"

# Ref: Intel Software Developer's Manual
# https://software.intel.com/en-us/articles/disclosure-of-hw-prefetcher-control-on-some-intel-processors
#
# Bit0: L2 Hardware Prefetcher Disable
# Bit1: L2 Adjacent Cache Line Prefetcher Disable
# Bit2: DCU prefetcher
# Bit3: DCU IP prefercher
while getopts :hde opt; do
	case ${opt} in
		h) usage
			;;
		e) echo "Enable prefetchers"
			for (( i=0; i<num_cpus; i++ ))
			do
				wrmsr -p $i 0x1a4 0x0
			done
			;;
		d) echo "Disable prefetchers" 
			for (( i=0; i<num_cpus; i++ ))
			do
				wrmsr -p $i 0x1a4 0xf
			done
			;;
	esac
done
