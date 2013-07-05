#!/bin/bash
set -e

export VERBOSE=1
export TRACE=1
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
	run rake test:install_deps RAILS_BUNDLES=no DOCTOOLS=no
	run rake test:cxx
	run rake test:oxt
fi

if [[ "$TEST_RUBY" = 1 ]]; then
	run rake test:install_deps DOCTOOLS=no
	run rake test:ruby
fi

if [[ "$TEST_NGINX" = 1 ]]; then
	run rake test:install_deps RAILS_BUNDLES=no DOCTOOLS=no
	run gem install rack daemon_controller --no-rdoc --no-ri
	run ./bin/passenger-install-nginx-module --auto --prefix=/tmp/nginx --auto-download
	run rake test:integration:nginx
fi

if [[ "$TEST_APACHE2" = 1 ]]; then
	run sudo apt-get install -y --no-install-recommends \
		apache2-mpm-worker apache2-threaded-dev
	run rake test:install_deps RAILS_BUNDLES=no DOCTOOLS=no
	run gem install rack --no-rdoc --no-ri
	run ./bin/passenger-install-apache2-module --auto
	run rake test:integration:apache2
fi

if [[ "$TEST_DEBIAN_PACKAGING" = 1 ]]; then
	run sudo apt-get install -y --no-install-recommends \
		devscripts debhelper rake apache2-mpm-worker apache2-threaded-dev \
		ruby1.8 ruby1.8-dev ruby1.9.1 ruby1.9.1-dev libev-dev gdebi-core \
		source-highlight
	run rake test:install_deps RAILS_BUNDLES=no
	run rake debian:dev
	run sudo gdebi -n pkg/ruby-passenger_*.deb
	run sudo gdebi -n pkg/ruby-passenger-dev_*.deb
	run sudo gdebi -n pkg/ruby-passenger-doc_*.deb
	run sudo gdebi -n pkg/libapache2-mod-passenger_*.deb
	run rvmsudo env LOCATIONS_INI=/usr/lib/ruby/vendor_ruby/phusion_passenger/locations.ini \
		rspec -f s -c test/integration_tests/native_packaging_spec.rb
	run env PASSENGER_LOCATION_CONFIGURATION_FILE=/usr/lib/ruby/vendor_ruby/phusion_passenger/locations.ini \
		rake test:integration:apache2 SUDO=1
fi
