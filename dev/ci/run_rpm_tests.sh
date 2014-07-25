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

echo '%_excludedocs 0' > /etc/rpm/macros.imgcreate
sed -i 's/nodocs//' /etc/yum.conf

cp /etc/hosts /etc/workaround-docker-2267/hosts
cat >> /etc/workaround-docker-2267/hosts <<EOF
127.0.0.1 passenger.test
127.0.0.1 mycook.passenger.test
127.0.0.1 zsfa.passenger.test
127.0.0.1 norails.passenger.test
127.0.0.1 1.passenger.test 2.passenger.test 3.passenger.test
127.0.0.1 4.passenger.test 5.passenger.test 6.passenger.test
127.0.0.1 7.passenger.test 8.passenger.test 9.passenger.test
EOF

yum_install /packages/*.x86_64.rpm /packages/*.noarch.rpm
perl -pi -e 's:/etc/hosts:/cte/hosts:g' /lib64/libnss_files.so.2 /lib64/libc.so.6 /lib64/libresolv.so.2
sed -i 's|/etc/hosts|/cte/hosts|g' /usr/lib/ruby/1.8/resolv.rb
chown -R app: /var/log/nginx /var/lib/nginx


cd /passenger
retry_run 3 /system/internal/setuser app \
	rake test:install_deps BASE_DEPS=yes SUDO=1
retry_run 3 /system/internal/setuser app \
	scl enable nodejs010 'rake test:install_deps NODE_MODULES=yes'
run /system/internal/setuser app \
	rake test:integration:native_packaging
run /system/internal/setuser app \
	env PASSENGER_LOCATION_CONFIGURATION_FILE=/usr/lib/ruby/site_ruby/1.8/phusion_passenger/locations.ini \
	scl enable python27 nodejs010 'rake test:integration:apache2'
run /system/internal/setuser app \
	env PASSENGER_LOCATION_CONFIGURATION_FILE=/usr/lib/ruby/site_ruby/1.8/phusion_passenger/locations.ini \
	scl enable python27 nodejs010 'rake test:integration:nginx'
