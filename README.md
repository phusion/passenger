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

## Installing as a gem

    gem build passenger.gemspec
    gem install passenger-x.x.x.gem

## Further reading

 * The `doc/` directory.
 * CONTRIBUTING.md.
 * https://www.phusionpassenger.com/support

## Legal

Phusion Passenger is a trademark of Hongli Lai & Ninh Bui.
