# Continuous integration test suite

This directory contains scripts that invoke the Passenger test suite. These scripts are invoked from the Passenger continuous integration environment, based on Jenkins.

The following diagrams explain how the different files fit together.

## Flow on Linux

~~~
Invoke: dev/ci/setup-host
   |     |
   |     +-- Load: dev/ci/scripts/setup-host
   |           |
   |           +-- Relax file permissions (if in Jenkins)
   |           |
   |           +-- Create cache directories
   |
Invoke: dev/ci/run-tests-with-docker <test name>
   |
   +-- Exec: Docker container
       Entrypoint: dev/ci/scripts/docker-entrypoint.sh
         |
         +-- Exec: dev/ci/scripts/debug-console-wrapper.sh dev/ci/scripts/docker-entrypoint-stage2.sh
               |
               +-- Invoke: dev/ci/scripts/docker-entrypoint-stage2.sh
               |     |
               |     +-- Load: dev/ci/lib/setup-container.sh <test name>
               |     |     |
               |     |     +-- Create test/config.json
               |     |     |
               |     |     +-- Relax home permission
               |     |     |
               |     |     +-- Remove previous build products
               |     |     |
               |     |     +-- Load: dev/ci/lib/set-container-envvars.sh
               |     |     |     |
               |     |     |     +-- Set RVM version and various envvars
               |     |     |
               |     |     +-- Load: dev/ci/tests/<test name>/setup
               |     |
               |     +-- Load: dev/ci/tests/<test name>/run
               |
               +-- (if docker-entrypoint-stage2.sh exited with an error,
               |    and DEBUG_CONSOLE is set to 0)
               |   Print error message and exit
               |
               +-- (if docker-entrypoint-stage2.sh exited with an error,
                    and DEBUG_CONSOLE is set to 1)
                      |
                      +-- Load: dev/ci/lib/set-container-envvars.sh
                      |     |
                      |     +-- Set RVM version and various envvars
                      |
                      +-- Invoke: bash
~~~

## Flow on macOS

~~~
Invoke: dev/ci/setup-host <test name>
   |      |
   |      +-- Relax file permissions (if in Jenkins)
   |      |
   |      +-- Create cache directories
   |      |
   |      +-- Exec: dev/ci/scripts/debug-console-wrapper.sh dev/ci/scripts/setup-host-natively.sh <test name>
   |           |
   |           +-- Invoke: dev/ci/scripts/setup-host-natively.sh
   |           |     |
   |           |     +-- Load: dev/ci/lib/setup-container.sh
   |           |           |
   |           |           +-- Create test/config.json
   |           |           |
   |           |           +-- Relax home permission
   |           |           |
   |           |           +-- Remove previous build products
   |           |           |
   |           |           +-- Load: dev/ci/lib/set-container-envvars.sh
   |           |           |     |
   |           |           |     +-- Set RVM version and various envvars
   |           |           |
   |           |           +-- Load: dev/ci/tests/<test name>/setup
   |           |
   |           +-- (if setup-host exited with an error,
   |           |    and DEBUG_CONSOLE is set to 0)
   |           |   Print error message and exit
   |           |
   |           +-- (if setup-host exited with an error,
   |                and DEBUG_CONSOLE is set to 1)
   |                 |
   |                 +-- Load: dev/ci/lib/set-container-envvars.sh
   |                 |     |
   |                 |     +-- Set RVM version and various envvars
   |                 |
   |                 +-- Invoke: bash
   |
Invoke: dev/ci/run-tests-natively <test name>
   |
   +-- Exec: dev/ci/scripts/debug-console-wrapper.sh dev/ci/scripts/run-tests-natively-stage2.sh <test name>
         |
         +-- Invoke: dev/ci/scripts/run-tests-natively-stage2.sh
         |     |
         |     +-- Load: dev/lib/set-container-envvars.sh
         |     |     |
         |     |     +-- Set RVM version and various envvars
         |     |
         |     +-- Load: dev/ci/tests/<test name>/run
         |
         +-- (if run-tests-natively-stage2.sh exited with an error,
         |    and DEBUG_CONSOLE is set to 0)
         |   Print error message and exit
         |
         +-- (if run-tests-natively-stage2.sh exited with an error,
              and DEBUG_CONSOLE is set to 1)
               |
               +-- Load: dev/ci/lib/set-container-envvars.sh
               |     |
               |     +-- Set RVM version and various envvars
               |
               +-- Invoke: bash
~~~
