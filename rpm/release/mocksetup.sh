#!/bin/sh

BUILD_VERBOSITY=${BUILD_VERBOSITY:-0}
[ $BUILD_VERBOSITY -ge 3 ] && set -x
set -e

repo=${1:-/var/lib/mock/passenger-build-repo}
etc=${2:-/etc/mock}
# For the written files & dirs, we want g+w, this isn't consistent enough
# umask 002

# For the non-groupinstall configs, pull the members out of the mock-comps.xml
prereqs=`egrep 'packagereq.*default' $(dirname $0)/mock-comps.xml | cut -d\> -f2 | cut -d\< -f1 | tr '\n' ' '`

for cfg in $etc/{fedora-{13,14},epel-5}-*.cfg
do
  [ $BUILD_VERBOSITY -ge 2 ] && echo $cfg
  dir=`dirname $cfg`
  base=`basename $cfg`
  new=$dir/passenger-$base
  perl -p - $repo "$prereqs" $cfg <<-'EOF' > $new
	# Warning <<- only kills TABS (ASCII 0x9) DO NOT CONVERT THESE
	# TABS TO SPACES -- IT WILL BREAK

	sub BEGIN {
	    our $repo = shift;
	    our $prereqs = shift;
	}

	s{opts\['root'\]\s*=\s*'}{${&}passenger-}; #';
	s{groupinstall [^']+}{$& Ruby build-passenger}; #'
	s{\binstall buildsys-build}{$& ruby ruby-devel $prereqs};
	s{^"""}{<<EndRepo . $&}e; #"

	[build-passenger]
	name=build-passenger
	baseurl=file://$repo
	EndRepo
EOF
  chgrp mock $new 2>/dev/null || true
  chmod g+w $new  2>/dev/null || true
done

mkdir -p $repo
cat `dirname $0`/mock-comps.xml > $repo/comps.xml


createrepo_volume=
if [ $BUILD_VERBOSITY -gt 1 ]; then
    createrepo_volume='-v'
else
    if [ $BUILD_VERBOSITY -le 0 ]; then
        createrepo_volume='-q'
    fi
fi

createrepo $createrepo_volume -g comps.xml $repo
chmod -R g+w $repo 2>/dev/null || true
