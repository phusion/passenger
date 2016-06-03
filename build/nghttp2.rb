# encoding: utf-8
#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2016 Phusion Holding B.V.
#
#  "Passenger", "Phusion Passenger" and "Union Station" are registered
#  trademarks of Phusion Holding B.V.
#
#  Permission is hereby granted, free of charge, to any person obtaining a copy
#  of this software and associated documentation files (the "Software"), to deal
#  in the Software without restriction, including without limitation the rights
#  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
#  copies of the Software, and to permit persons to whom the Software is
#  furnished to do so, subject to the following conditions:
#
#  The above copyright notice and this permission notice shall be included in
#  all copies or substantial portions of the Software.
#
#  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
#  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
#  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
#  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
#  THE SOFTWARE.

NGHTTP2_SOURCE_DIR = SOURCE_ROOT / 'src/cxx_supportlib/vendor-copy/nghttp2'
NGHTTP2_TARGET = NGHTTP2_OUTPUT_DIR / 'lib/.libs/libnghttp2.a'
NGHTTP2_DEPENDENCY_TARGETS = [NGHTTP2_TARGET.to_s]

task :nghttp2 => NGHTTP2_TARGET

dependencies = [
  'src/cxx_supportlib/vendor-copy/nghttp2/configure',
  'src/cxx_supportlib/vendor-copy/nghttp2/configure.ac',
  'src/cxx_supportlib/vendor-copy/nghttp2/Makefile.in',
  'src/cxx_supportlib/vendor-copy/nghttp2/Makefile.am'
]
file(NGHTTP2_OUTPUT_DIR / 'Makefile' => dependencies) do
  cc = CC
  cxx = CXX
  if OPTIMIZE && LTO
    cc = "#{cc} -flto"
    cxx = "#{cxx} -flto"
  end
  sh "mkdir -p #{shesc NGHTTP2_OUTPUT_DIR}" if !NGHTTP2_OUTPUT_DIR.directory?
  # Prevent 'make' from regenerating autotools files
  sh "cd #{shesc NGHTTP2_OUTPUT_DIR} && (touch aclocal.m4 configure Makefile.in config.guess config.sub depcomp missing || true)"
  sh "cd #{shesc NGHTTP2_OUTPUT_DIR} && sh #{shesc NGHTTP2_SOURCE_DIR}/configure " \
    "--disable-shared --enable-static --disable-dependency-tracking " \
    "--disable-silent-rules --disable-app --disable-hpack-tools " \
    "--disable-asio-lib --disable-examples --disable-python-bindings " \
    "--disable-xmltest --without-libxml2 --without-jemalloc --without-spdylay " \
    "--without-mruby --without-neverbleed --without-boost " +
    # nghttp2's configure script may select a different default compiler than we
    # do, so we force our compiler choice.
    "CC=#{shesc cc} " \
    "CXX=#{shesc cxx} " \
    "CFLAGS=#{shesc EXTRA_CFLAGS} " \
    "orig_CFLAGS=1"
end

nghttp2_sources = Dir[NGHTTP2_SOURCE_DIR / 'lib/{*.c,*.h}']
file(NGHTTP2_TARGET => [NGHTTP2_OUTPUT_DIR / 'Makefile'] + nghttp2_sources) do
  sh "rm -f #{shesc NGHTTP2_OUTPUT_DIR}/lib/libnghttp2.la"
  sh "cd #{shesc NGHTTP2_OUTPUT_DIR}/lib && make -j2 libnghttp2.la"
end

task 'nghttp2:clean' do
  sh "rm -rf #{shesc NGHTTP2_OUTPUT_DIR}/*"
end

task :clean => 'nghttp2:clean'

def nghttp2_libs
  la_contents = File.open(NGHTTP2_OUTPUT_DIR / 'lib/.libs/libnghttp2.la', 'r') do |f|
    f.read
  end
  la_contents =~ /dependency_libs='(.+)'/
  "#{shesc NGHTTP2_OUTPUT_DIR}/lib/.libs/libnghttp2.a #{$1}".strip
end
