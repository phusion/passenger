# Introduction

This is the [Phusion Passenger web application server](https://www.phusionpassenger.com/).

## Regular installation

You can install either Phusion Passenger for Apache or for Nginx. Run either of
the following programs as root:

    ./bin/passenger-install-apache2-module

-OR-

    ./bin/passenger-install-nginx-module

That's it. :)

For troubleshooting, configuration and tips, please read the corresponding Users Guide:

 * doc/Users guide Apache.html
 * doc/Users guide Nginx.html

These files are included in the source tarball, and may also be viewed online on [our website](https://www.phusionpassenger.com/support).

## Installing via a gem

You may also generate a .gem file, and then install that. First, make sure that you have the following software installed:

 * All usual Phusion Passenger dependencies.
 * [Mizuho](https://github.com/FooBarWidget/mizuho).

Next, run:

    rake package

The gem will be available under the `pkg` folder.

## Further reading

 * The `doc/` directory.
 * CONTRIBUTING.md.
 * https://www.phusionpassenger.com/support

## Legal

Phusion Passenger is a trademark of Hongli Lai & Ninh Bui.
