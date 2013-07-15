# Phusion Passenger: a fast and robust web server and application server for Ruby, Python and Node.js

[Phusion Passenger](https://www.phusionpassenger.com/) is a web server and application server, designed to be fast, robust and lightweight. It runs your web apps with the least amount of hassle by taking care of almost all administrative heavy lifting for you. Advanced administration tools allow you to gain deep insight into your web applications' operations and to keep your servers healthy. Phusion Passenger is polyglot by design, and currently supports Ruby (Rack), Python (WSGI) and Node.js.

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
