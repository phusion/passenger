#!/bin/bash
# This script is run by Travis, to execute tests in the CI environment.

set -e

PASSENGER_ROOT=`dirname "$0"`
PASSENGER_ROOT=`cd "$PASSENGER_ROOT/../.." && pwd`
PASSENGER_ROOT_ON_DOCKER_HOST=${PASSENGER_ROOT_ON_DOCKER_HOST:-$PASSENGER_ROOT}

if [[ "$CACHE_DIR_ON_DOCKER_HOST" != "" ]]; then
	CACHE_DIR=/host_cache
else
	CACHE_DIR=/tmp
	CACHE_DIR_ON_DOCKER_HOST=/tmp
fi

export VERBOSE=1
export TRACE=1
export DEVDEPS_DEFAULT=no
export rvmsudo_secure_path=1

if [[ -e /etc/workaround-docker-2267/hosts ]]; then
	HOSTS_FILE=/etc/workaround-docker-2267/hosts
	sudo workaround-docker-2267
	find /usr/local/rvm/rubies/*/lib/ruby -name resolv.rb | sudo xargs sed -i 's|/etc/hosts|/cte/hosts|g'
else
	HOSTS_FILE=/etc/hosts
fi

sudo sh -c "cat >> $HOSTS_FILE" <<EOF
127.0.0.1 passenger.test
127.0.0.1 mycook.passenger.test
127.0.0.1 zsfa.passenger.test
127.0.0.1 norails.passenger.test
127.0.0.1 1.passenger.test 2.passenger.test 3.passenger.test
127.0.0.1 4.passenger.test 5.passenger.test 6.passenger.test
127.0.0.1 7.passenger.test 8.passenger.test 9.passenger.test
EOF

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

function apt_get_update() {
	if [[ "$apt_get_updated" = "" ]]; then
		apt_get_updated=1
		if [[ "$TEST_DEBIAN_PACKAGING" = 1 ]]; then
			if ! [[ -e /usr/bin/add-apt-repository ]]; then
				run sudo apt-get update
				run sudo apt-get install -y --no-install-recommends python-software-properties
				if ! [[ -e /usr/bin/add-apt-repository ]]; then
					run sudo apt-get install -y --no-install-recommends software-properties-common
				fi
			fi
			run sudo add-apt-repository -y ppa:phusion.nl/misc
		fi
		run sudo apt-get update
	fi
}

function install_test_deps_with_doctools()
{
	if [[ "$install_test_deps_with_doctools" = "" ]]; then
		install_test_deps_with_doctools=1
		retry_run 3 rake test:install_deps BASE_DEPS=yes DOCTOOLS=yes
	fi
}

function install_base_test_deps()
{
	if [[ "$install_base_test_deps" = "" ]]; then
		install_base_test_deps=1
		retry_run 3 rake test:install_deps BASE_DEPS=yes
	fi
}

function install_node_and_modules()
{
	if [[ "$install_node_and_modules" = "" ]]; then
		install_node_and_modules=1
		run curl --fail -O http://nodejs.org/dist/v0.10.20/node-v0.10.20-linux-x64.tar.gz
		run tar xzf node-v0.10.20-linux-x64.tar.gz
		export PATH=`pwd`/node-v0.10.20-linux-x64/bin:$PATH
		retry_run 3 rake test:install_deps NODE_MODULES=yes
	fi
}

if [[ "$MAGNUM" != "" ]]; then
	run sudo sh -c "echo 127.0.0.1 magnum >> /etc/hosts"
fi

run uname -a
run lsb_release -a
run sudo tee /etc/dpkg/dpkg.cfg.d/02apt-speedup >/dev/null <<<"force-unsafe-io"
run cp test/config.json.travis test/config.json

# Relax permissions on home directory so that the application root
# permission checks pass.
run chmod g+x,o+x $HOME

if [[ "$TEST_RUBY_VERSION" != "" ]]; then
	echo "$ rvm use $TEST_RUBY_VERSION"
	if [[ -f ~/.rvm/scripts/rvm ]]; then
		source ~/.rvm/scripts/rvm
	else
		source /usr/local/rvm/scripts/rvm
	fi
	rvm use $TEST_RUBY_VERSION
	if [[ "$TEST_RUBYGEMS_VERSION" = "" ]]; then
		run gem --version
	fi
fi

if [[ "$TEST_RUBYGEMS_VERSION" != "" ]]; then
	retry_run 3 rvm install rubygems $TEST_RUBYGEMS_VERSION
	run gem --version
fi

if [[ "$TEST_CXX" = 1 ]]; then
	install_base_test_deps
	run rake test:cxx
	run rake test:oxt
fi

if [[ "$TEST_RUBY" = 1 ]]; then
	retry_run 3 rake test:install_deps BASE_DEPS=yes RAILS_BUNDLES=yes
	run rake test:ruby
fi

if [[ "$TEST_NODE" = 1 ]]; then
	install_node_and_modules
	run rake test:node
fi

if [[ "$TEST_NGINX" = 1 ]]; then
	install_base_test_deps
	install_node_and_modules
	run ./bin/passenger-install-nginx-module --auto --prefix=/tmp/nginx --auto-download
	run rake test:integration:nginx
fi

if [[ "$TEST_APACHE2" = 1 ]]; then
	apt_get_update
	run sudo apt-get install -y --no-install-recommends \
		apache2-mpm-worker apache2-threaded-dev
	install_base_test_deps
	install_node_and_modules
	run ./bin/passenger-install-apache2-module --auto #--no-update-config
	run rvmsudo ./bin/passenger-install-apache2-module --auto --no-compile
	run rake test:integration:apache2
fi

if [[ "$TEST_STANDALONE" = 1 ]]; then
	install_base_test_deps
	run rake test:integration:standalone
fi

if [[ "$TEST_DEBIAN_PACKAGING" = 1 ]]; then
	apt_get_update
	run sudo apt-get install -y --no-install-recommends \
		devscripts debhelper rake apache2-mpm-worker apache2-threaded-dev \
		ruby1.8 ruby1.8-dev ruby1.9.1 ruby1.9.1-dev rubygems libev-dev gdebi-core \
		source-highlight
	install_test_deps_with_doctools
	install_node_and_modules
	run rake debian:dev debian:dev:reinstall
	run rake test:integration:native_packaging SUDO=1
	run env PASSENGER_LOCATION_CONFIGURATION_FILE=/usr/lib/ruby/vendor_ruby/phusion_passenger/locations.ini \
		rake test:integration:apache2 SUDO=1
fi

if [[ "$TEST_RPM_PACKAGING" = 1 ]]; then
	if [[ "$TEST_RPM_BUILDING" != 0 ]]; then
		pushd packaging/rpm
		run mkdir -p "$CACHE_DIR/passenger_rpm"
		run mkdir -p "$CACHE_DIR/passenger_rpm/cache"
		run mkdir "$CACHE_DIR/passenger_rpm/output"
		run ./build -S "$PASSENGER_ROOT_ON_DOCKER_HOST/packaging/rpm" \
			-P "$PASSENGER_ROOT_ON_DOCKER_HOST" \
			-o "$CACHE_DIR_ON_DOCKER_HOST/passenger_rpm/output" \
			-c "$CACHE_DIR_ON_DOCKER_HOST/passenger_rpm/cache" \
			-d el6 -a x86_64
		popd >/dev/null
	fi

	echo "------------- Testing built RPMs -------------"
	run cp "test/config.json.rpm-automation" "test/config.json"
	run mkdir -p "$CACHE_DIR/passenger_rpm/output/el6-x86_64"
	if [[ "$TEST_RPM_BUILDING" != 0 ]]; then
		run rm "$CACHE_DIR/passenger_rpm/output/el6-x86_64"/*.src.rpm
	fi
	run docker run --rm \
		-v "$PASSENGER_ROOT_ON_DOCKER_HOST/packaging/rpm:/system:ro" \
		-v "$PASSENGER_ROOT_ON_DOCKER_HOST:/passenger" \
		-v "$CACHE_DIR_ON_DOCKER_HOST/passenger_rpm/output/el6-x86_64:/packages:ro" \
		-e "APP_UID=`id -u`" \
		-e "APP_GID=`id -g`" \
		phusion/passenger_rpm_automation \
		/system/internal/my_init --skip-runit --skip-startup-files --quiet -- \
		/system/internal/inituidgid \
		/bin/bash -c "cd /passenger && exec ./dev/ci/run_rpm_tests.sh"
	run cp "test/config.json.travis" "test/config.json"
fi

if [[ "$TEST_SOURCE_PACKAGING" = 1 ]]; then
	apt_get_update
	run sudo apt-get install -y --no-install-recommends source-highlight
	install_test_deps_with_doctools
	run rspec -f s -c test/integration_tests/source_packaging_test.rb
fi
