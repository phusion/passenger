#!/bin/sh

dir=`dirname $0`
mirrors="$dir/mirrors"

echo "D: $dir"

for path in fedora rhel
do
    for mirror in $(cat $mirrors); do
        if [ "${mirror:(-1)}" != "/" ] ; then
            mirror="$mirror/"
        fi
        echo "$mirror$path/\$releasever/\$basearch/"
    done > $path/mirrors
done
