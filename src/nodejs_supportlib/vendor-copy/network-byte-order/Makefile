test: lib test/lib node_modules
	TEST_LIB_PATH="../../lib" ./node_modules/.bin/mocha \
		--recursive \
		--reporter dot \
		--check-leaks \
		--ui tdd

test-coveralls: build/lib-coverage test/lib node_modules
	TEST_LIB_PATH="../../build/lib-coverage" ./node_modules/.bin/mocha \
		--recursive \
		--reporter mocha-lcov-reporter \
		--ui tdd | \
		./node_modules/coveralls/bin/coveralls.js

test-cov: build/coverage.html

build/coverage.html: build/lib-coverage test/lib node_modules
	TEST_LIB_PATH="../../build/lib-coverage" ./node_modules/.bin/mocha \
		--recursive \
		--reporter html-cov \
		--ui tdd \
		> $@

build/lib-coverage: build lib node_modules
	./node_modules/.bin/jscoverage \
		--no-highlight \
		lib \
		build/lib-coverage

node_modules: package.json
	npm install
	touch $@

build:
	if [ ! -d $@ ]; then \
		mkdir $@; \
	fi;

clean:
	rm -rf build

.PHONY: test test-cov test-coveralls clean
