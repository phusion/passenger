#!/bin/bash
# This script is run by Jenkins, to execute tests in the CI environment.

set -e

PASSENGER_ROOT=`dirname "$0"`
PASSENGER_ROOT=`cd "$PASSENGER_ROOT/../.." && pwd`

if [[ "$WORKSPACE" = "" ]]; then
	echo "Please set WORKSPACE."
	exit 1
fi

JENKINS_CACHE_DIR="$WORKSPACE/.jenkins_cache"
mkdir -p "$JENKINS_CACHE_DIR"

# run_travis.sh defaults COMPILE_CONCURRENCY to 2, but
# Jenkins jobs are run on a single machine so we'll want
# a COMPILE_CONCURRENCY of 1.
COMPILE_CONCURRENCY=${COMPILE_CONCURRENCY:-1}

# Relax permissions. Necessary for unit tests which test permissions.
echo "Relaxing permissions"
umask u=rwx,g=rx,o=rx
(
	set -x
	chmod g+rx,o+rx .
	find * -type f -print0 | xargs -0 -n 512 chmod g+r,o+r
	find * -type d -print0 | xargs -0 -n 512 chmod g+rx,o+rx

	# Create this file now because otherwise it would be owned by root,
	# which Jenkins cannot remove.
	touch test/test.log
)

function run_exec()
{
	echo "$ $@"
	exec "$@"
}

# We do not use the my_init inside the image to work around
# a bug in baseimage-docker 0.9.12: pressing Ctrl-C does
# not properly result in a non-zero exit status.
run_exec docker run --rm \
	-v "$PASSENGER_ROOT:/passenger" \
	-v /var/run/docker.sock:/docker.sock \
	-v "$JENKINS_CACHE_DIR:/host_cache" \
	-e "DOCKER_HOST=unix:///docker.sock" \
	-e "DOCKER_GID=`getent group docker | cut -d: -f3`" \
	-e "PASSENGER_ROOT_ON_DOCKER_HOST=$PASSENGER_ROOT" \
	-e "CACHE_DIR_ON_DOCKER_HOST=$JENKINS_CACHE_DIR" \
	-e "APP_UID=`id -u`" \
	-e "APP_GID=`id -g`" \
	-e "SUDO=$SUDO" \
	-e "COMPILE_CONCURRENCY=$COMPILE_CONCURRENCY" \
	-e "TEST_CXX=$TEST_CXX" \
	-e "TEST_RUBY=$TEST_RUBY" \
	-e "TEST_NODE=$TEST_NODE" \
	-e "TEST_RUBY_VERSION=$TEST_RUBY_VERSION" \
	-e "TEST_RUBYGEMS_VERSION=$TEST_RUBYGEMS_VERSION" \
	-e "TEST_NGINX=$TEST_NGINX" \
	-e "TEST_APACHE2=$TEST_APACHE2" \
	-e "TEST_STANDALONE=$TEST_STANDALONE" \
	-e "TEST_SOURCE_PACKAGING=$TEST_SOURCE_PACKAGING" \
	phusion/apachai-hopachai-sandbox \
	python /passenger/packaging/rpm/internal/scripts/my_init --skip-runit --skip-startup-files --quiet -- \
	/passenger/dev/ci/inituidgid \
	/sbin/setuser appa \
	/bin/bash -lc "cd /passenger && exec ./dev/ci/run_travis.sh"
