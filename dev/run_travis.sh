#!/bin/bash
set -e

export VERBOSE=1
export TRACE=1
export DEVDEPS_DEFAULT=no
export rvmsudo_secure_path=1

sudo sh -c 'cat >> /etc/hosts' <<EOF
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
		run rake test:install_deps BASE_DEPS=yes DOCTOOLS=yes
	fi
}

function install_base_test_deps()
{
	if [[ "$install_base_test_deps" = "" ]]; then
		install_base_test_deps=1
		rake test:install_deps BASE_DEPS=yes
	fi
}

function install_node_and_modules()
{
	if [[ "$install_node_and_modules" = "" ]]; then
		install_node_and_modules=1
		curl -O http://nodejs.org/dist/v0.10.20/node-v0.10.20-linux-x64.tar.gz
		tar xzvf node-v0.10.20-linux-x64.tar.gz
		export PATH=`pwd`/node-v0.10.20-linux-x64/bin:$PATH
		run rake test:install_deps NODE_MODULES=yes
	fi
}

run uname -a
run lsb_release -a
sudo tee /etc/dpkg/dpkg.cfg.d/02apt-speedup >/dev/null <<<"force-unsafe-io"
cp test/config.json.travis test/config.json

# Relax permissions on home directory so that the application root
# permission checks pass.
chmod g+x,o+x $HOME

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
	run rvm install rubygems $TEST_RUBYGEMS_VERSION
	run gem --version
fi

if [[ "$TEST_CXX" = 1 ]]; then
	run rake test:install_deps BASE_DEPS=yes
	run rake test:cxx
	run rake test:oxt
fi

if [[ "$TEST_RUBY" = 1 ]]; then
	run rake test:install_deps BASE_DEPS=yes RAILS_BUNDLES=yes
	run rake test:ruby
fi

if [[ "$TEST_NODE" = 1 ]]; then
	install_node_and_modules
	run rake test:node
fi

if [[ "$TEST_NGINX" = 1 ]]; then
	install_base_test_deps
	run ./bin/passenger-install-nginx-module --auto --prefix=/tmp/nginx --auto-download
	run rake test:integration:nginx
fi

if [[ "$TEST_APACHE2" = 1 ]]; then
	apt_get_update
	run sudo apt-get install -y --no-install-recommends \
		apache2-mpm-worker apache2-threaded-dev
	install_base_test_deps
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
	run rake debian:dev debian:dev:reinstall
	run rake test:integration:native_packaging SUDO=1
	run env PASSENGER_LOCATION_CONFIGURATION_FILE=/usr/lib/ruby/vendor_ruby/phusion_passenger/locations.ini \
		rake test:integration:apache2 SUDO=1
fi

if [[ "$TEST_SOURCE_PACKAGING" = 1 ]]; then
	apt_get_update
	run sudo apt-get install -y --no-install-recommends source-highlight
	install_test_deps_with_doctools
	run rspec -f s -c test/integration_tests/source_packaging_test.rb
fi
