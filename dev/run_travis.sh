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

if [[ "$TEST_RUBY_VERSION" != "" ]]; then
	echo "$ rvm use $TEST_RUBY_VERSION"
	if [[ -f ~/.rvm/scripts/rvm ]]; then
		source ~/.rvm/scripts/rvm
	else
		source /usr/local/rvm/scripts/rvm
	fi
	rvm use $TEST_RUBY_VERSION
	if [[ "$TEST_RUBYGEMS_VERSION" = "" ]]; then
		echo "$ gem --version"
		gem --version
	fi
fi

if [[ "$TEST_RUBYGEMS_VERSION" != "" ]]; then
	echo "$ rvm install rubygems $TEST_RUBYGEMS_VERSION"
	rvm install rubygems $TEST_RUBYGEMS_VERSION
	echo "$ gem --version"
	gem --version
fi

if [[ "$TEST_FULL_COMPILE" = 1 ]]; then
	echo "$ gem install rack --no-rdoc --no-ri"
	gem install rack --no-rdoc --no-ri
	echo "$ ./bin/passenger-install-apache2-module --auto"
	./bin/passenger-install-apache2-module --auto
	echo "$ rake nginx"
	rake nginx
	echo "$ rake test/cxx/CxxTestMain"
	rake test/cxx/CxxTestMain
	echo "$ rake test/oxt/oxt_test_main"
	rake test/oxt/oxt_test_main
fi

if [[ "$TEST_CXX" = 1 ]]; then
	echo "$ rake test:install_deps RAILS_BUNDLES=no"
	rake test:install_deps RAILS_BUNDLES=no
	echo "$ rake test:cxx"
	rake test:cxx
	echo "$ rake test:oxt"
	rake test:oxt
fi

if [[ "$TEST_RUBY" = 1 ]]; then
	echo "$ rake test:install_deps"
	rake test:install_deps
	echo "$ rake test:ruby"
	rake test:ruby
fi

if [[ "$TEST_NGINX" = 1 ]]; then
	echo "$ rake test:install_deps RAILS_BUNDLES=no"
	rake test:install_deps RAILS_BUNDLES=no
	echo "$ gem install rack daemon_controller --no-rdoc --no-ri"
	gem install rack daemon_controller --no-rdoc --no-ri
	echo "$ ./bin/passenger-install-nginx-module --auto --prefix=/tmp/nginx --auto-download"
	./bin/passenger-install-nginx-module --auto --prefix=/tmp/nginx --auto-download
	echo "$ rake test:integration:nginx"
	rake test:integration:nginx
fi

if [[ "$TEST_APACHE2" = 1 ]]; then
	echo "$ rake test:install_deps RAILS_BUNDLES=no"
	rake test:install_deps RAILS_BUNDLES=no
	echo "$ gem install rack --no-rdoc --no-ri"
	gem install rack --no-rdoc --no-ri
	echo "$ rake apache2"
	rake apache2
	echo "$ rake test:integration:apache2"
	rake test:integration:apache2
fi
