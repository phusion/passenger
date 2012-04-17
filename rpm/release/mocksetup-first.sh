#!/bin/bash -e
# Checks whether all the prerequities for the package:rpm task are available.

BUILD_VERBOSITY=${BUILD_VERBOSITY:-0}
[ $BUILD_VERBOSITY -ge 3 ] && set -x

declare -a required_packages=(mock)
if grep -iq fedora /etc/redhat-release ; then
		# fedora-packager has rpmbuild-md5 for the SRPM
		required_packages=( ${required_packages[@]} fedora-packager )
else
		required_packages=( ${required_packages[@]} rpm-build )
fi

while getopts ':p:' opt
do
	case $opt in
		p)
			required_packages=( ${required_packages[@]} $OPTARG )
		;;
		\?)
			echo "Invalid flag -$OPTARG" >&2
			exit 255
		;;
		\:)
			echo "missing argument to -$OPTARG" >&2
			exit 127
		;;
	esac
done
shift $(($OPTIND - 1))

repo=${1:-/var/lib/mock/passenger-build-repo}
etc=${2:-/etc/mock}

if [ "$(id -u)" == "0" ]; then
	cat <<-EOF >&2
		It is a very bad idea to run this build as root.
		Use a less-privileged user. If that user is not a sudoer, you may need to run
		a few commands as root before the first run. (Those commands will be determined
		the first time through)
EOF
	exit 1
fi

mock_installed=`rpm -q mock | grep -v 'not installed' | wc -l`
cfg_count=`ls -1 $etc/passenger-* 2>/dev/null | wc -l`
in_mock_group=`groups | grep mock | wc -l`

# Fedora has a utility to do this, but I don't know if RHEL does, plus it requires another package install, just do it by hand
mkdir -p `rpm -E '%{_topdir}'`/{SOURCES,SPECS,SRPMS,RPMS}

if [[ ( $cfg_count == 0 && ! -w $etc ) || ! ( -d $etc || -w $etc/.. )	 || ! ( -w $repo || -w $repo/.. ) ]] ; then
	run_setup=1
fi

declare -a yum_pkgs

for pkg in "${required_packages[@]}"
do
	if [ `rpm -q $pkg | grep -v 'not installed' | wc -l` -eq 0 ] ; then
		yum_pkgs=(${yum_pkgs[@]} $pkg)
	fi
done

if [[ "$run_setup" = "1" || ! $in_mock_group == 1 || ${#yum_pkgs[@]} != 0 ]] ; then
	echo "There is some setup required before building packages."
	echo "We will run sudo to do the following:"

	if [[ ${#yum_pkgs[@]} != 0 ]] ; then
		echo "	# yum -y install ${yum_pkgs[@]}"
	fi

	if [[ ! "$in_mock_group" == "1" ]] ; then
		echo "	# usermod -a -G mock $USER"
		echo "		(add you to the mock group, you will need to log back in for this to take effect)"
	fi

	if [[ "$run_setup" == "1" ]] ; then
		echo "	# `dirname $0`/mocksetup.sh"
		echo "		(sets up the dependencies for mock)"
	fi
	echo
	echo "Hit return to continue, ^C to cancel"
	read

	if [[ ! $mock_installed == 1 || ! $createrepo_installed == 1 ]] ; then
		sudo yum -y install "${yum_pkgs[@]}"
	fi
	if [[ ! "$in_mock_group" == "1" ]] ; then
		sudo usermod -a -G mock $USER
	fi
	if [[ "$run_setup" == "1" ]] ; then
		sudo `dirname $0`/mocksetup.sh $repo $etc
	fi

	if [[ ! "$in_mock_group" == "1" ]] ; then
		echo "You have been added to the mock group. Please relogin for this to take effect, and re-run 'rake package:rpm'."
		exit 4
	fi
fi

