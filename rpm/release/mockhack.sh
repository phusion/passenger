#!/bin/sh

srpm=$1
srpm_base=`basename $srpm`
cfg=${2:-fedora-14-i386}
mock="mock -r $cfg"

set -x
set -e

#$mock clean
$mock -v init
$mock -v installdeps $srpm
$mock --copyin $srpm /tmp
$mock -v chroot "rm -f /var/lib/rpm/__db* ; rpmbuild --rebuild /tmp/$srpm_base"
