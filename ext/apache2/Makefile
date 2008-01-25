APXS=apxs2
APACHECTL=apache2ctl

all:
	$(APXS) -c mod_rails.c

# XXX should not need the -c option but for some reason it's required :-(
install:
	$(APXS) -i -c mod_rails.c

clean:
	rm -rf mod_rails.o mod_rails.lo mod_rails.slo mod_rails.la .libs

start:
	$(APACHECTL) start
restart:
	$(APACHECTL) restart
stop:
	$(APACHECTL) stop
