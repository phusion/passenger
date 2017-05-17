#!/bin/bash
set -e

RESET=$(echo -e "\033[0m")
BOLD=$(echo -e "\033[1m")
YELLOW=$(echo -e "\033[33m")
BLUE_BG=$(echo -e "\033[44m")

function header()
{
	local title="$1"
	echo "${BLUE_BG}${YELLOW}${BOLD}${title}${RESET}"
	echo "------------------------------------------"
}

function header2()
{
	local title="$1"
	echo "### ${BOLD}${title}${RESET}"
	echo
}

function run()
{
	echo "+ $*"
	"$@"
}

function run_exec()
{
	echo "+ exec $*"
	exec "$@"
}

function retry_run()
{
	local reset='\x1B[0m'
	local red='\x1B[31m'
	local yellow='\x1B[33m'

	local max_tries="$1"
	local number=2
	shift

	echo "+ $*"
	while true; do
		if "$@"; then
			return 0
		elif [[ $number -le $max_tries ]]; then
			echo -e "${yellow}The command \"$*\" failed. Retrying, $number of $max_tries:${reset}"
			(( number++ ))
		else
			echo -e "${red}The command \"$*\" failed after $max_tries attempts. Giving up.${reset}"
			return 1
		fi
	done
}

function autodetect_environment()
{
	echo "Environment autodetection results:"
	if [[ "$JENKINS_HOME" != "" ]]; then
		echo "Running in Jenkins: yes"
		export IN_JEKINS=true
		export CACHE_DIR="$JENKINS_HOME/cache/$JOB_NAME/executor-$EXECUTOR_NUMBER"
	else
		echo "Running in Jenkins: no"
		export IN_JENKINS=false
		export CACHE_DIR="$PASSENGER_ROOT/.ci_cache"
	fi
	if [[ -e /usr/bin/sw_vers ]]; then
		echo "Operating system: macOS"
		export OS=macos
	else
		echo "Operating system: Linux"
		export OS=linux
	fi
	echo "Cache directory: $CACHE_DIR"
}

function sanity_check_environment()
{
	if $IN_JENKINS; then
		if [[ "$JOB_NAME" = "" ]]; then
			echo "ERROR: Jenkins environment detected, but JOB_NAME environment variable not set." >&2
			return 1
		fi
		if [[ "$EXECUTOR_NUMBER" = "" ]]; then
			echo "ERROR: Jenkins environment detected, but EXECUTOR_NUMBER environment variable not set." >&2
			return 1
		fi
	fi
}

# The following is necessary to make the C++ tests work.
# They invoke Ruby scripts which require gems installed
# by Bundler, but these scripts are not invoked with
# Bundler, so they can only find these gems through GEM_PATH.
function add_bundler_path_to_gem_path()
{
	local bundle_path
	
	if bundle_path=$(bundle show rake); then
		bundle_path=$(dirname "$bundle_path")
		bundle_path=$(dirname "$bundle_path")
		echo "Adding $bundle_path to GEM_PATH"
		export GEM_PATH="$bundle_path:$GEM_PATH"

		local bundle_bin_path="$bundle_path/bin"
		echo "Adding $bundle_bin_path to PATH"
		export PATH="$bundle_bin_path:$PATH"
	fi
}

function _cleanup()
{
	set +e
	local pids
	pids=$(jobs -p)
	if [[ "$pids" != "" ]]; then
		# shellcheck disable=SC2086
		kill $pids 2>/dev/null
	fi
	if [[ $(type -t cleanup) == function ]]; then
		cleanup
	fi
}

trap _cleanup EXIT
