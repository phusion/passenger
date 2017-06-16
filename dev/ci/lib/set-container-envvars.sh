#!/bin/bash
# Do not `set -e` here because debug-console-wrapper.sh
# relies on this fact.

export DEVDEPS_DEFAULT=no
# shellcheck disable=SC2153
export DEPS_TARGET="$CACHE_DIR/bundle"
export USE_CCACHE=true
export COMPILE_CONCURRENCY=${COMPILE_CONCURRENCY:-2}
export YARN_ARGS="--cache-folder='$CACHE_DIR/yarn'"
export CCACHE_DIR="$CACHE_DIR/ccache"
export CCACHE_COMPRESS=1
export CCACHE_COMPRESSLEVEL=3
export CCACHE_BASEDIR="$PASSENGER_ROOT"
export CCACHE_SLOPPINESS=time_macros
# We want Bundler invocations to be explicit. For example,
# when running 'rake test:install_deps', we do not want
# to invoke Bundler there because the goal might be to
# install the Rake version as specified in the Gemfile,
# which we may not have yet.
export NOEXEC_DISABLE=1

if [[ "$OS" = macos ]]; then
	# Ensure that Homebrew tools can be found
	export PATH=$PATH:/usr/local/bin
else
	export LC_CTYPE=C.UTF-8
fi

if [[ -f ~/.rvm/scripts/rvm ]]; then
	# shellcheck source=/dev/null
	source ~/.rvm/scripts/rvm
else
	# shellcheck source=/dev/null
	source /usr/local/rvm/scripts/rvm
fi

if [[ "$TEST_RUBY_VERSION" != "" ]]; then
	header2 "Using Ruby version $TEST_RUBY_VERSION"
	run rvm use "$TEST_RUBY_VERSION"
	echo
fi

# RVM's cd override causes problems (probably thanks to bash
# error handling being weird and quirky:
# https://news.ycombinator.com/item?id=14321213)
unset cd
