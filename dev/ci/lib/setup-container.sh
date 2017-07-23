#!/bin/bash

if ! grep -q passenger.test /etc/hosts; then
	header2 "Updating /etc/hosts"
	sudo sh -c "cat >> /etc/hosts" <<EOF
127.0.0.1 passenger.test
127.0.0.1 mycook.passenger.test
127.0.0.1 zsfa.passenger.test
127.0.0.1 norails.passenger.test
127.0.0.1 1.passenger.test 2.passenger.test 3.passenger.test
127.0.0.1 4.passenger.test 5.passenger.test 6.passenger.test
127.0.0.1 7.passenger.test 8.passenger.test 9.passenger.test
EOF
	echo
fi

header2 "Creating test/config.json"
if [[ "$OS" = linux ]]; then
	run cp test/config.json.travis test/config.json
else
	sed -e "s/_USER_/$USER/" test/config.json.travis-osx > test/config.json
fi
echo "+ Done."
echo

# Relax permissions on home directory so that the application root
# permission checks pass.
header2 "Relaxing home permission"
run chmod g+x,o+x "$HOME"
echo

header2 "Removing previous build products"
run rm -rf buildout
echo

# shellcheck source=../lib/set-container-envvars.sh
source "$PASSENGER_ROOT/dev/ci/lib/set-container-envvars.sh"

header "Running test-specific preparations"
# shellcheck source=/dev/null
source "$PASSENGER_ROOT/dev/ci/tests/$1/setup"
echo '+ Done.'
echo
