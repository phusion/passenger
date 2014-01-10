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
for F in "doc/Users guide.html" "doc/Users guide Apache.html" "doc/Users guide Nginx.html" "doc/Users guide Standalone.html" "doc/Packaging.html"; do
	if [[ ! -f "$F" ]]; then
		echo > "$F"
	fi
done

cp test/config.json.rpm-automation test/config.json

rake test:install_deps DEVDEPS_DEFAULT=false BASE_DEPS=true RAILS_BUNDLES=true SUDO=1
rake rpm:local rpm:local:reinstall
rake test:integration:native_packaging \
	LOCATIONS_INI=/usr/lib/ruby/site_ruby/1.8/phusion_passenger/locations.ini \
	NATIVE_PACKAGING_METHOD=rpm \
	SUDO=1
env PASSENGER_LOCATION_CONFIGURATION_FILE=/usr/lib/ruby/site_ruby/1.8/phusion_passenger/locations.ini \
	rake test:integration:apache2 \
	SUDO=1
