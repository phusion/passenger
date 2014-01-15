#!/bin/bash
set -ex

sudo sh -c 'cat >> /etc/hosts' <<EOF
127.0.0.1 passenger.test
127.0.0.1 mycook.passenger.test
127.0.0.1 zsfa.passenger.test
127.0.0.1 norails.passenger.test
127.0.0.1 1.passenger.test 2.passenger.test 3.passenger.test
127.0.0.1 4.passenger.test 5.passenger.test 6.passenger.test
127.0.0.1 7.passenger.test 8.passenger.test 9.passenger.test
EOF

# Don't care about the docs.
for F in "Users guide.html" "Users guide Apache.html" "Users guide Nginx.html" "Users guide Standalone.html" "Packaging.html" "Security of user switching support.html" "Architectural overview.html"; do
	if [[ ! -f "doc/$F" ]]; then
		echo > "doc/$F"
	fi
done

cp test/config.json.rpm-automation test/config.json

rake test:install_deps DEVDEPS_DEFAULT=false BASE_DEPS=true RAILS_BUNDLES=true
rake rpm:local rpm:local:reinstall
rake test:integration:native_packaging SUDO=1
env PASSENGER_LOCATION_CONFIGURATION_FILE=/usr/lib/ruby/site_ruby/1.8/phusion_passenger/locations.ini \
	rake test:integration:apache2 \
	SUDO=1
