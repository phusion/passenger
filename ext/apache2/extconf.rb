m = open("Makefile", "wb")
data =<<EOF
SHELL=/bin/sh

PASSENGER_DIR=../..

all install static install-so install-rb:
\t[ -d $(PASSENGER_DIR) ] || (echo "ERROR: Passenger dir $(PASSENGER_DIR) not found" && exit)
\techo "Installing passenger"
\tUSE_VENDORED_LIBEV=no $(PASSENGER_DIR)/bin/passenger-install-apache2-module --auto
\tcd $(PASSENGER_DIR) && find ext/apache2 -name \*.a -print -delete
\tcd $(PASSENGER_DIR) && rm -v test/stub/wsgi/passenger_wsgi.pyc  2> /dev/null || echo "passenger_wsgi.pyc already removed"
\tcd $(PASSENGER_DIR) && mv -v ext/ruby/*/passenger_native_support.so lib/ 2>/dev/null || [ -f lib/passenger_native_support.so ] ||echo "ERROR: lib/passenger_native_support.so not found."
\tcd $(PASSENGER_DIR) && rm -rv ext/ruby/ruby-*-linux || echo "ruby-*-linux already removed"


EOF
m.puts data
m.close

