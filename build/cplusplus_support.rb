#  Phusion Passenger - http://www.modrails.com/
#  Copyright (c) 2010 Phusion
#
#  "Phusion Passenger" is a trademark of Hongli Lai & Ninh Bui.
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

# Rake functions for compiling/linking C++ stuff.

def compile_c(source, flags = "#{PlatformInfo.portability_cflags} #{EXTRA_CXXFLAGS}")
	sh "#{CC} #{flags} -c #{source}"
end

def compile_cxx(source, flags = "#{PlatformInfo.portability_cflags} #{EXTRA_CXXFLAGS}")
	sh "#{CXX} #{flags} -c #{source}"
end

def create_static_library(target, sources)
	# On OS X, 'ar cru' will sometimes fail with an obscure error:
	#
	#   ar: foo.a is a fat file (use libtool(1) or lipo(1) and ar(1) on it)
	#   ar: foo.a: Inappropriate file type or format
	#
	# So here we delete the ar file before creating it, which bypasses this problem.
	sh "rm -rf #{target}"
	sh "ar cru #{target} #{sources}"
	sh "ranlib #{target}"
end

def create_executable(target, sources, linkflags = "#{PlatformInfo.portability_cflags} #{EXTRA_CXXFLAGS} #{PlatformInfo.portability_ldflags} #{EXTRA_LDFLAGS}")
	sh "#{CXX} #{sources} -o #{target} #{linkflags}"
end

def create_c_executable(target, sources, linkflags = "#{PlatformInfo.portability_cflags} #{EXTRA_CXXFLAGS} #{PlatformInfo.portability_ldflags} #{EXTRA_LDFLAGS}")
	sh "#{CC} #{sources} -o #{target} #{linkflags}"
end

def create_shared_library(target, sources, flags = "#{PlatformInfo.portability_cflags} #{EXTRA_CXXFLAGS} #{PlatformInfo.portability_ldflags} #{EXTRA_LDFLAGS}")
	if RUBY_PLATFORM =~ /darwin/
		shlib_flag = "-flat_namespace -bundle -undefined dynamic_lookup"
	else
		shlib_flag = "-shared"
	end
	sh "#{CXX} #{shlib_flag} #{sources} -fPIC -o #{target} #{flags}"
end
