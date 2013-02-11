#!/bin/bash
set -e

if [[ "$TEST_RUBY_VERSION" != "" ]]; then
	echo "$ rvm use $TEST_RUBY_VERSION"
	source ~/.rvm/scripts/rvm
	rvm use $TEST_RUBY_VERSION
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
	rake test:install_deps
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
