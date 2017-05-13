#!/bin/bash
set -eo pipefail

SELFDIR=$(dirname "$0")
SELFDIR=$(cd "$SELFDIR" && pwd)
PASSENGER_ROOT=$(cd "$SELFDIR/../../.." && pwd)
# shellcheck source=../lib/functions.sh
source "$SELFDIR/../lib/functions.sh"

# shellcheck source=../lib/setup-container.sh
source "$PASSENGER_ROOT/dev/ci/lib/setup-container.sh"
