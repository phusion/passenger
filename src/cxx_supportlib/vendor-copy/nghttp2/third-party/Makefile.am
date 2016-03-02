# nghttp2 - HTTP/2 C Library

# Copyright (c) 2014 Tatsuhiro Tsujikawa

# Permission is hereby granted, free of charge, to any person obtaining
# a copy of this software and associated documentation files (the
# "Software"), to deal in the Software without restriction, including
# without limitation the rights to use, copy, modify, merge, publish,
# distribute, sublicense, and/or sell copies of the Software, and to
# permit persons to whom the Software is furnished to do so, subject to
# the following conditions:

# The above copyright notice and this permission notice shall be
# included in all copies or substantial portions of the Software.

# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
# LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
# OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
# WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

AM_CPPFLAGS = @DEFS@

if ENABLE_THIRD_PARTY

noinst_LTLIBRARIES = libhttp-parser.la
libhttp_parser_la_SOURCES = \
	http-parser/http_parser.c \
	http-parser/http_parser.h

if HAVE_NEVERBLEED
noinst_LTLIBRARIES += libneverbleed.la
libneverbleed_la_CPPFLAGS = @OPENSSL_CFLAGS@
libneverbleed_la_LIBADD = @OPENSSL_LIBS@
libneverbleed_la_SOURCES = neverbleed/neverbleed.c neverbleed/neverbleed.h
endif # HAVE_NEVERBLEED

if HAVE_MRUBY

EXTRA_DIST = build_config.rb mruby/*

.PHONY: all-local clean mruby

mruby:
	MRUBY_CONFIG="${srcdir}/build_config.rb" \
	BUILD_DIR="${abs_builddir}/mruby/build" \
	INSTALL_DIR="${abs_builddir}/mruby/build/install/bin" \
	CC="${CC}" CXX="${CXX}" LD="${LD}" \
	CFLAGS="${CPPFLAGS} ${CFLAGS}" CXXFLAGS="${CPPFLAGS} ${CXXFLAGS}" \
	LDFLAGS="${LDFLAGS}" \
	"${srcdir}/mruby/minirake" -f "${srcdir}/mruby/Rakefile"

all-local: mruby

clean-local:
	-rm -rf "${abs_builddir}/mruby/build"

endif # HAVE_MRUBY

endif # ENABLE_THIRD_PARTY
