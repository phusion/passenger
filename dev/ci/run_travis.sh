#!/bin/bash
# This script is run by Travis, to execute tests in the CI environment.

set -e

PASSENGER_ROOT=`dirname "$0"`
PASSENGER_ROOT=`cd "$PASSENGER_ROOT/../.." && pwd`
PASSENGER_ROOT_ON_DOCKER_HOST=${PASSENGER_ROOT_ON_DOCKER_HOST:-$PASSENGER_ROOT}

if [[ "$CACHE_DIR_ON_DOCKER_HOST" != "" ]]; then
	CACHE_DIR=/host_cache
else
	CACHE_DIR="$PWD/cache"
	CACHE_DIR_ON_DOCKER_HOST="$PWD/cache"
fi

COMPILE_CONCURRENCY=${COMPILE_CONCURRENCY:-2}

TEST_DYNAMIC_WITH_NGINX_VERSION=1.9.15

export VERBOSE=1
export TRACE=1
export DEVDEPS_DEFAULT=no
export rvmsudo_secure_path=1
export LC_CTYPE=C.UTF-8
export DEPS_TARGET="$CACHE_DIR/bundle"
export USE_CCACHE=true
export CCACHE_DIR="$CACHE_DIR/ccache"
export CCACHE_COMPRESS=1
export CCACHE_COMPRESS_LEVEL=3
unset BUNDLE_GEMFILE

if [[ -e /etc/workaround-docker-2267 ]]; then
	HOSTS_FILE=/etc/workaround-docker-2267/hosts
	sudo ln -s /etc/hosts /etc/workaround-docker-2267/hosts
	sudo workaround-docker-2267
	find /usr/local/rvm/rubies/*/lib/ruby -name resolv.rb | sudo xargs sed -i 's|/etc/hosts|/cte/hosts|g'
else
	HOSTS_FILE=/etc/hosts
fi

mkdir -p "$DEPS_TARGET"
mkdir -p "$CCACHE_DIR"

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
		run sudo apt-get update
	fi
}

function brew_update() {
	if [[ "$brew_updated" = "" ]]; then
		brew_updated=1
		run brew update
	fi
}

function rake_test_install_deps()
{
	# We do not use Bundler here because the goal might be to
	# install the Rake version as specified in the Gemfile,
	# which we may not have yet.
	run env NOEXEC_DISABLE=1 rake test:install_deps "$@"
	local code=$?
	if [[ $code != 0 ]]; then
		return $code
	fi

	local bundle_path
	if ! bundle_path=`bundle show rake`; then
		bundle_path=`bundle show nokogiri`
	fi
	bundle_path=`dirname "$bundle_path"`
	bundle_path=`dirname "$bundle_path"`
	echo "Adding bundle path $bundle_path to GEM_PATH"
	export GEM_PATH="$bundle_path:$GEM_PATH"
	if [[ "$TRAVIS_OS_NAME" == 'osx' ]]; then
		run brew install ccache
	fi
}

function install_test_deps_with_doctools()
{
	if [[ "$install_test_deps_with_doctools" = "" ]]; then
		install_test_deps_with_doctools=1
		retry_run 3 rake_test_install_deps BASE_DEPS=yes DOCTOOLS=yes
	fi
}

function install_base_test_deps()
{
	if [[ "$install_base_test_deps" = "" ]]; then
		install_base_test_deps=1
		if [[ "$TRAVIS_OS_NAME" == 'osx' ]]; then
			run brew install ccache
		fi
		retry_run 3 rake_test_install_deps BASE_DEPS=yes
	fi
}

function install_node_and_modules()
{
	if [[ "$install_node_and_modules" = "" ]]; then
		install_node_and_modules=1
		if [[ -e /host_cache ]]; then
			if [[ "$TRAVIS_OS_NAME" == 'osx' ]]; then
				if [[ ! -e /host_cache/node-v4.7.2-darwin-x64.tar.gz ]]; then
					run curl --fail -L -o /host_cache/node-v4.7.2-darwin-x64.tar.gz \
					https://nodejs.org/dist/v4.7.2/node-v4.7.2-darwin-x64.tar.gz
				fi
				run tar xzf /host_cache/node-v4.7.2-darwin-x64.tar.gz
			else
				if [[ ! -e /host_cache/node-v4.7.2-linux-x64.tar.gz ]]; then
					run curl --fail -L -o /host_cache/node-v4.7.2-linux-x64.tar.gz \
					https://nodejs.org/dist/v4.7.2/node-v4.7.2-linux-x64.tar.gz
				fi
				run tar xzf /host_cache/node-v4.7.2-linux-x64.tar.gz
			fi
		else
			if [[ "$TRAVIS_OS_NAME" == 'osx' ]]; then
				run curl --fail -L -O https://nodejs.org/dist/v4.7.2/node-v4.7.2-darwin-x64.tar.gz
				run tar xzf node-v4.7.2-darwin-x64.tar.gz
			else
				run curl --fail -L -O https://nodejs.org/dist/v4.7.2/node-v4.7.2-linux-x64.tar.gz
				run tar xzf node-v4.7.2-linux-x64.tar.gz
			fi
		fi
		if [[ "$TRAVIS_OS_NAME" == 'osx' ]]; then
			export PATH=`pwd`/node-v4.7.2-darwin-x64/bin:$PATH
		else
			export PATH=`pwd`/node-v4.7.2-linux-x64/bin:$PATH
		fi
		retry_run 3 rake_test_install_deps NODE_MODULES=yes
	fi
}

run uname -a
if [[ "$TRAVIS_OS_NAME" == 'osx' ]]; then
	run sysctl -a
	echo '$ sed -e "s/_USER_/'$USER'/" test/config.json.travis-osx > test/config.json'
	sed -e "s/_USER_/$USER/" test/config.json.travis-osx > test/config.json
else
	run lsb_release -a
	run sudo tee /etc/dpkg/dpkg.cfg.d/02apt-speedup >/dev/null <<<"force-unsafe-io"
	run cp test/config.json.travis test/config.json
fi

# Relax permissions on home directory so that the application root
# permission checks pass.
run chmod g+x,o+x $HOME

if [[ "$TEST_RUBY_VERSION" != "" ]]; then
	if [[ -f ~/.rvm/scripts/rvm ]]; then
		source ~/.rvm/scripts/rvm
	else
		source /usr/local/rvm/scripts/rvm
	fi
	echo "$ rvm list"
	rvm list
	echo "$ rvm install $TEST_RUBY_VERSION"
	rvm install $TEST_RUBY_VERSION
	echo "$ rvm use $TEST_RUBY_VERSION"
	rvm use $TEST_RUBY_VERSION
	if [[ "$TEST_RUBYGEMS_VERSION" = "" ]]; then
		run gem --version
	fi
fi

if [[ "$TEST_RUBYGEMS_VERSION" != "" ]]; then
	retry_run 3 rvm install rubygems $TEST_RUBYGEMS_VERSION
	run gem --version
fi

ORIG_GEM_PATH="$GEM_PATH"

if [[ "$INSTALL_ALL_DEPS" = 1 ]]; then
	run rake_test_install_deps DEVDEPS_DEFAULT=yes
	INSTALL_DEPS=0
fi

if [[ "$TEST_CXX" = 1 ]]; then
	install_base_test_deps
	run bundle exec drake -j$COMPILE_CONCURRENCY test:cxx
	run bundle exec rake test:oxt
fi

if [[ "$TEST_RUBY" = 1 ]]; then
	retry_run 3 rake_test_install_deps BASE_DEPS=yes
	run bundle exec drake -j$COMPILE_CONCURRENCY test:ruby
fi

if [[ "$TEST_USH" = 1 ]]; then
	retry_run 3 rake_test_install_deps BASE_DEPS=yes USH_BUNDLES=yes
	export PASSENGER_CONFIG="$PWD/bin/passenger-config"
	run "$PASSENGER_CONFIG" install-standalone-runtime --auto

	# RVM is bad and should feel bad
	builtin pushd src/ruby_supportlib/phusion_passenger/vendor/union_station_hooks_core
	bundle exec rake spec:travis TRAVIS_WITH_SUDO=1
	builtin popd

	builtin pushd src/ruby_supportlib/phusion_passenger/vendor/union_station_hooks_rails
	bundle exec rake spec:travis GEM_BUNDLE_PATH="$DEPS_TARGET"
	builtin popd
fi

if [[ "$TEST_NODE" = 1 ]]; then
	install_node_and_modules
	run bundle exec drake -j$COMPILE_CONCURRENCY test:node
fi

if [[ "$TEST_NGINX" = 1 ]]; then
	install_base_test_deps
	install_node_and_modules
	run ./bin/passenger-install-nginx-module --auto --prefix=/tmp/nginx --auto-download
	run bundle exec drake -j$COMPILE_CONCURRENCY test:integration:nginx

	run curl -sSLO http://www.nginx.org/download/nginx-$TEST_DYNAMIC_WITH_NGINX_VERSION.tar.gz
	run tar zxf nginx-$TEST_DYNAMIC_WITH_NGINX_VERSION.tar.gz
	run cd nginx-$TEST_DYNAMIC_WITH_NGINX_VERSION
	run ./configure --add-dynamic-module=$(../bin/passenger-config --nginx-addon-dir)
	run make
	run cd ..
fi

if [[ "$TEST_APACHE2" = 1 ]]; then
	if [[ "$TRAVIS_OS_NAME" == 'osx' ]]; then
		brew_update
		run brew install pcre openssl
		if [[ "`sw_vers -productVersion | sed 's/^10\.\(.*\)/\1>=12.0/' | bc -l`" == "1" ]] ; then
			run brew install apr apr-util
			run brew link apr apr-util --force
			export APR_CONFIG=`brew --prefix`/opt/apr/bin/apr-1-config
			export APU_CONFIG=`brew --prefix`/opt/apr-util/bin/apu-1-config
		fi
	else
		apt_get_update
		run sudo apt-get install -y --no-install-recommends \
		apache2-mpm-worker apache2-threaded-dev
	fi
	install_base_test_deps
	install_node_and_modules
	run ./bin/passenger-install-apache2-module --auto #--no-update-config
	if [[ "$TRAVIS_OS_NAME" == 'osx' ]]; then
		# rvmsudo only preserves env vars matching /^(rvm|gemset|http_|PATH|IRBRC)|RUBY|GEM/
		# https://github.com/rvm/rvm/blob/aae6505001e2d6b5e4dc9a355c18ffcbd073bab2/bin/rvmsudo#L83
		run sudo -E ./bin/passenger-install-apache2-module --auto --no-compile
	else
		run rvmsudo ./bin/passenger-install-apache2-module --auto --no-compile
	fi
	run bundle exec drake -j$COMPILE_CONCURRENCY test:integration:apache2
fi

if [[ "$TEST_STANDALONE" = 1 ]]; then
	install_base_test_deps
	run bundle exec drake -j$COMPILE_CONCURRENCY test:integration:standalone
fi

if [[ "$TEST_SOURCE_PACKAGING" = 1 ]]; then
	if [[ "$TRAVIS_OS_NAME" == 'osx' ]]; then
		brew_update
		run brew install source-highlight
	else
		apt_get_update
		run sudo apt-get install -y --no-install-recommends source-highlight
	fi
	install_test_deps_with_doctools
	run bundle _1.11.2_ exec rspec -f s -c test/integration_tests/source_packaging_test.rb
fi
if [[ "$TRAVIS_OS_NAME" == 'osx' ]]; then
	trap - EXIT
fi
