#!/bin/bash

# Permission check
if [ $(id -u) != 0 ]
then
  echo "Root permission is required to run this script!"
  exit 1
fi

for core in $(cat /sys/devices/system/cpu/cpu*/topology/thread_siblings_list | cut -f2 -d ','| sort -n | uniq)
do
	echo "$core is disabled"
	echo 0 > /sys/devices/system/cpu/cpu$core/online
done
