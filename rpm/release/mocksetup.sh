#!/bin/sh

repo=${1:-/var/lib/mock/passenger-build-repo}

# For the written files & dirs, we want g+w, this isn't consistent enough
# umask 002

set -e

for cfg in /etc/mock/{fedora-{13,14},epel-5}-*.cfg
do
  echo $cfg
  dir=`dirname $cfg`
  base=`basename $cfg`
  new=$dir/passenger-$base
  perl -p - $repo $cfg <<-'EOF' > $new
	# Warning <<- only kills TABS (ASCII 0x9) DO NOT CONVERT THESE
	# TABS TO SPACES -- IT WILL BREAK

	sub BEGIN {
	    our $repo = shift;
	}

	s{opts\['root'\]\s*=\s*'}{${&}passenger-}; #';
	s{groupinstall [^']+}{$& Ruby build-passenger}; #'
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
createrepo -g comps.xml $repo
chmod -R g+w $repo 2>/dev/null || true
