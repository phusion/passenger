#!/bin/bash
set -eo pipefail

SELFDIR=/passenger/dev/ci/scripts
PASSENGER_ROOT=/passenger
# shellcheck source=../lib/functions.sh
source "$SELFDIR/../lib/functions.sh"

# shellcheck source=../lib/setup-container.sh
source "$PASSENGER_ROOT/dev/ci/lib/setup-container.sh"
add_bundler_path_to_gem_path
echo
echo

header "Running test suite: $1"
# shellcheck source=/dev/null
source "$PASSENGER_ROOT/dev/ci/tests/$1/run"
