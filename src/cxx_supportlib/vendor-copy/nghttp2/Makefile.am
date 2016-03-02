# nghttp2 - HTTP/2 C Library

# Copyright (c) 2012 Tatsuhiro Tsujikawa

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
SUBDIRS = lib third-party src examples python tests integration-tests \
	doc contrib script

# Now with python setuptools, make uninstall will leave many files we
# cannot easily remove (e.g., easy-install.pth).  Disable it for
# distcheck rule.
AM_DISTCHECK_CONFIGURE_FLAGS = --disable-python-bindings

ACLOCAL_AMFLAGS = -I m4

dist_doc_DATA = README.rst

EXTRA_DIST = nghttpx.conf.sample proxy.pac.sample android-config android-make \
	Dockerfile.android

.PHONY: clang-format

# Format source files using clang-format.  Don't format source files
# under third-party directory since we are not responsible for thier
# coding style.
clang-format:
	CLANGFORMAT=`git config --get clangformat.binary`; \
	test -z $${CLANGFORMAT} && CLANGFORMAT="clang-format"; \
	$${CLANGFORMAT} -i lib/*.{c,h} lib/includes/nghttp2/*.h \
	src/*.{c,cc,h} src/includes/nghttp2/*.h examples/*.{c,cc} \
	tests/*.{c,h}
