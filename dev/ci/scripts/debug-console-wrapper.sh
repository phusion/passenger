#!/bin/bash
set -eo pipefail

SELFDIR=$(dirname "$0")
SELFDIR=$(cd "$SELFDIR" && pwd)
PASSENGER_ROOT=$(cd "$SELFDIR/../../.." && pwd)
# shellcheck source=lib/functions.sh
source "$SELFDIR/../lib/functions.sh"

if ! "$@"; then
	echo
	echo "-----------------------------"
	echo
	echo "*** An error occurred ***"
	if [[ "$DEBUG_CONSOLE" == 1 ]]; then
		echo "DEBUG_CONSOLE set to 1, so launching a debugging console..."
		echo
		# shellcheck source=../lib/set-container-envvars.sh
		set +e
		source "$PASSENGER_ROOT/dev/ci/lib/set-container-envvars.sh"
		header2 "Launching bash"
		bash -l
	else
		echo "If you want to debug this, run '$CI_COMMAND' with the environment variable DEBUG_CONSOLE=1."
	fi
	exit 1
fi
