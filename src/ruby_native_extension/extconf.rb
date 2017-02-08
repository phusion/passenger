#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2010-2017 Phusion Holding B.V.
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
require 'mkmf'

$LIBS << " -lpthread" if $LIBS !~ /-lpthread/
$CFLAGS << " -g"

if RUBY_PLATFORM =~ /solaris/
	have_library('xnet')
	$CFLAGS << " -D_XPG4_2"
	$CFLAGS << " -D__EXTENSIONS__"
	if RUBY_PLATFORM =~ /solaris2.9/
		$CFLAGS << " -D__SOLARIS9__"
	end
end

have_header('alloca.h')
have_header('ruby/version.h')
have_header('ruby/io.h')
have_header('ruby/thread.h')
have_var('ruby_version')
have_func('rb_thread_io_blocking_region', 'ruby/io.h')
have_func('rb_thread_call_without_gvl', 'ruby/thread.h')

with_cflags($CFLAGS) do
	create_makefile('passenger_native_support')
	if RUBY_PLATFORM =~ /solaris/
		# Fix syntax error in Solaris /usr/ccs/bin/make.
		# https://code.google.com/p/phusion-passenger/issues/detail?id=999
		makefile = File.read("Makefile")
		makefile.sub!(/^ECHO = .*/, "ECHO = echo")
		File.open("Makefile", "w") do |f|
			f.write(makefile)
		end
	elsif RUBY_PLATFORM =~ /darwin/
		# The OS X Clang 503.0.38 update (circa March 15 2014) broke
		# /usr/bin/ruby's mkmf. mkmf inserts -multiply_definedsuppress
		# into the Makefile, but that flag is no longer supported by
		# Clang. We remove this manually.
		makefile = File.read("Makefile")
		makefile.sub!(/-multiply_definedsuppress/, "")
		File.open("Makefile", "w") do |f|
			f.write(makefile)
		end
	end
end

