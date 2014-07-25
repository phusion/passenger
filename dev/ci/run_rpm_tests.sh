#!/bin/bash
# This script is run by run_travis.sh, to execute RPM packaging tests in the CI environment.

set -e

function run()
{
	echo "$ $@"
	"$@"
}

function retry_run()
{
	local reset='\x1B[0m'
	local red='\x1B[31m'
	local yellow='\x1B[33m'

	local max_tries="$1"
	local number=2
	shift

	echo "$ $@"
	while true; do
		if "$@"; then
			return 0
		elif [[ $number -le $max_tries ]]; then
			echo -e "${yellow}The command \"$@\" failed. Retrying, $number of $max_tries:${reset}"
			(( number++ ))
		else
			echo -e "${red}The command \"$@\" failed after $max_tries attempts. Giving up.${reset}"
			return 1
		fi
	done
}

function yum_install()
{
	run yum install -y --skip-broken --enablerepo centosplus "$@"
}

export CACHING=false
export DEVDEPS_DEFAULT=no

cd /passenger

echo '%_excludedocs 0' > /etc/rpm/macros.imgcreate
sed -i 's/nodocs//' /etc/yum.conf

run yum_install /packages/*.x86_64.rpm /packages/*.noarch.rpm
retry_run 3 rake test:install_deps BASE_DEPS=yes
chown -R app: /var/log/nginx /var/lib/nginx

run rake test:integration:native_packaging
run /system/internal/setuser app \
	env PASSENGER_LOCATION_CONFIGURATION_FILE=/usr/lib/ruby/site_ruby/1.8/phusion_passenger/locations.ini \
	scl enable python27 nodejs010 'rake test:integration:apache2'
run /system/internal/setuser app \
	env PASSENGER_LOCATION_CONFIGURATION_FILE=/usr/lib/ruby/site_ruby/1.8/phusion_passenger/locations.ini \
	scl enable python27 nodejs010 'rake test:integration:nginx'
