#!/bin/sh

# Generalize this eventually
sDir=$HOME/rpmbuild/SRPMS
bindir=`dirname $0`

# No dist for SRPM
rpmbuild-md5 --define 'dist %nil' -bs passenger.spec

srpm=`ls -1t $HOME/rpmbuild/SRPMS | head -1`

set -e
set -x

rm -rf stage
mkdir -p stage

for cfg in {fedora-{14,13},epel-5}-{x86_64,i386}
do
  iDir=`echo $cfg | sed -e 's#-#/#g'`
  if [ ! -d stage/$iDir ] ; then
      echo ======================= $cfg
      mkdir -p stage/$iDir
      $bindir/mockhack.sh $sDir/$srpm $cfg
      cp /var/lib/mock/$cfg/root/builddir/build/RPMS/* stage/$iDir
  fi
done

mv stage/epel stage/rhel
mkdir stage/SRPMS
cp $sDir/$srpm stage/SRPMS
rpm --addsign `find stage -type f`

