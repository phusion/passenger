#!/bin/bash
set -eo pipefail

SELFDIR=/passenger/dev/ci/scripts
PASSENGER_ROOT=/passenger
# shellcheck source=../lib/functions.sh
source "$SELFDIR/../lib/functions.sh"
cd "$PASSENGER_ROOT"

header "Inside Docker container"

autodetect_environment
echo

export CI_COMMAND="./dev/ci/run-tests-with-docker $*"
exec "$PASSENGER_ROOT/dev/ci/scripts/debug-console-wrapper.sh" \
	"$PASSENGER_ROOT/dev/ci/scripts/docker-entrypoint-stage2.sh" "$@"
