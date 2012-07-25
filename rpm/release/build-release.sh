#!/bin/sh -e

BUILD_VERBOSITY=${BUILD_VERBOSITY:-0}
[ $BUILD_VERBOSITY -ge 3 ] && set -x

reldir=`dirname $0`
stage=./stage-release
rm -rf $stage
mkdir -p $stage/{SRPMS,SOURCES}
ln -s `readlink -f $reldir/mirrors` $stage/SOURCES/mirrors-passenger
ln -s `readlink -f $reldir/RPM-GPG-KEY-stealthymonkeys` $stage/SOURCES

rpmbuild-md5 --define "_topdir $stage" --define 'dist %nil' -bs passenger-release.spec
rm -rf $stage/{SOURCES,BUILD*,RPMS,SPECS}
srpm=`ls -1t $stage/SRPMS/*rpm | head -1`

for ver in {epel-5,fedora-{13,14}}
do
	echo --------- $ver
	xdir=$stage/`echo $ver | tr '-' '/'`/x86_64
	idir=`echo $xdir | sed -e 's/x86_64/i386/'`
	mock -r passenger-$ver-x86_64 $srpm
	mkdir -p $xdir $idir
	cp /var/lib/mock/passenger-$ver-x86_64/result/*noarch.rpm $xdir
	cp /var/lib/mock/passenger-$ver-x86_64/result/*noarch.rpm $idir
	cd $xdir/..
	short=`ls -1t x86_64/*rpm | head -1 | perl -pe 's{.*/(.*)-[^-]+-[^-]+(.noarch.rpm)}{\1\2}'`
	ln -s x86_64/*rpm $short
	cd -
done

mv $stage/epel $stage/rhel
# Don't resign symlinks
# -- arguably this should be done once for each file, since they're copied
rpm --addsign `find $stage -type f`
