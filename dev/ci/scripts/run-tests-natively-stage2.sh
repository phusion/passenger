#!/bin/bash
set -eo pipefail

SELFDIR=$(dirname "$0")
SELFDIR=$(cd "$SELFDIR" && pwd)
PASSENGER_ROOT=$(cd "$SELFDIR/../../.." && pwd)
# shellcheck source=lib/functions.sh
source "$SELFDIR/../lib/functions.sh"

# shellcheck source=lib/set-container-envvars.sh
source "$SELFDIR/../lib/set-container-envvars.sh"
add_bundler_path_to_gem_path
echo

header "Running test suite: $1"
# shellcheck source=/dev/null
source "$PASSENGER_ROOT/dev/ci/tests/$1/run"
