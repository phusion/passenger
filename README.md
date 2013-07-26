# Phusion Passenger: a fast and robust web server and application server for Ruby, Python and Node.js

[Phusion Passenger](https://www.phusionpassenger.com/) is a web server and application server, designed to be fast, robust and lightweight. It runs your web apps with the least amount of hassle by taking care of almost all administrative heavy lifting for you. Advanced administration tools allow you to gain deep insight into your web applications' operations and to keep your servers healthy. Phusion Passenger is polyglot by design, and currently supports Ruby (Rack), Python (WSGI) and Node.js.

## Regular installation

You can install either Phusion Passenger for Apache or for Nginx. Basically, installation involves running one of these commands:

    ./bin/passenger-install-apache2-module

-OR-

    ./bin/passenger-install-nginx-module

That's it. :) However on some systems installation may require some more steps. You may have to run `sudo` or `rvmsudo`, and you may have to relax permissions on some directories, etc. Detailed fool-proof installation instructions can be found in the documentation:

 * [Apache version](http://www.modrails.com/documentation/Users%20guide%20Apache.html#tarball_generic_install)
 * [Nginx version](http://www.modrails.com/documentation/Users%20guide%20Nginx.html#tarball_generic_install)

For troubleshooting, configuration and tips, please also refer to the above documentation. For further support, please refer to [the Phusion Passenger support page](https://www.phusionpassenger.com/support).

## Installing as a gem

    gem build passenger.gemspec
    gem install passenger-x.x.x.gem

## Further reading

 * The `doc/` directory.
 * CONTRIBUTING.md.
 * https://www.phusionpassenger.com/support

## Legal

Phusion Passenger is a trademark of Hongli Lai & Ninh Bui.
