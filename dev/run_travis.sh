#!/bin/bash
set -e

if [[ "$RUBY_VERSION" != "" ]]; then
	echo "$ rvm use $RUBY_VERSION"
	source ~/.rvm/scripts/rvm
	rvm use $RUBY_VERSION
	echo "$ gem --version"
	gem --version
fi

if [[ "$TEST_FULL_COMPILE" = 1 ]]; then
	echo "$ rake apache2"
	rake apache2
	echo "$ rake nginx"
	rake nginx
	echo "$ rake test/cxx/CxxTestMain"
	rake test/cxx/CxxTestMain
	echo "$ rake test/oxt/oxt_test_main"
	rake test/oxt/oxt_test_main
fi

if [[ "$TEST_CXX" = 1 ]]; then
	echo "$ rake test:cxx"
	rake test:cxx
	echo "$ rake test:oxt"
	rake test:oxt
fi

if [[ "$TEST_RUBY" = 1 ]]; then
	echo "$ rake test:ruby"
	rake test:ruby
fi
