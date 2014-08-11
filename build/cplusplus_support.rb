#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2010-2014 Phusion
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

def run_compiler(*command)
	PhusionPassenger.require_passenger_lib 'utils/ansi_colors' if !defined?(PhusionPassenger::Utils::AnsiColors)
	show_command = command.join(' ')
	puts show_command
	if !system(*command)
		if $? && $?.exitstatus == 4
			# This probably means the compiler ran out of memory.
			msg = "<b>" +
			      "-----------------------------------------------\n" +
			      "Your compiler failed with the exit status 4. This " +
			      "probably means that it ran out of memory. To solve " +
			      "this problem, try increasing your swap space: " +
			      "https://www.digitalocean.com/community/articles/how-to-add-swap-on-ubuntu-12-04" +
			      "</b>"
			fail(PhusionPassenger::Utils::AnsiColors.ansi_colorize(msg))
		elsif $? && $?.termsig == 9
			msg = "<b>" +
			      "-----------------------------------------------\n" +
			      "Your compiler was killed by the operating system. This " +
			      "probably means that it ran out of memory. To solve " +
			      "this problem, try increasing your swap space: " +
			      "https://www.digitalocean.com/community/articles/how-to-add-swap-on-ubuntu-12-04" +
			      "</b>"
			fail(PhusionPassenger::Utils::AnsiColors.ansi_colorize(msg))
		else
			fail "Command failed with status (#{$? ? $?.exitstatus : 1}): [#{show_command}]"
		end
	end
end

def compile_c(source, flags = "#{EXTRA_PRE_CFLAGS} #{EXTRA_CFLAGS}")
	run_compiler "#{CC} #{flags} -c #{source}"
end

def compile_cxx(source, flags = "#{EXTRA_PRE_CXXFLAGS} #{EXTRA_CXXFLAGS}")
	run_compiler "#{CXX} #{flags} -c #{source}"
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

def create_executable(target, sources, linkflags = "#{EXTRA_PRE_CXXFLAGS} #{EXTRA_PRE_C_LDFLAGS} #{EXTRA_CXXFLAGS} #{PlatformInfo.portability_cxx_ldflags} #{EXTRA_CXX_LDFLAGS}")
	run_compiler "#{CXX} #{sources} -o #{target} #{linkflags}"
end

def create_c_executable(target, sources, linkflags = "#{EXTRA_PRE_CFLAGS} #{EXTRA_PRE_CXX_LDFLAGS}#{EXTRA_CFLAGS} #{PlatformInfo.portability_c_ldflags} #{EXTRA_C_LDFLAGS}")
	run_compiler "#{CC} #{sources} -o #{target} #{linkflags}"
end

def create_shared_library(target, sources, flags = "#{EXTRA_PRE_CXXFLAGS} #{EXTRA_PRE_CXX_LDFLAGS} #{EXTRA_CXXFLAGS} #{PlatformInfo.portability_cxx_ldflags} #{EXTRA_CXX_LDFLAGS}")
	if PlatformInfo.os_name == "macosx"
		shlib_flag = "-flat_namespace -bundle -undefined dynamic_lookup"
	else
		shlib_flag = "-shared"
	end
	if PhusionPassenger::PlatformInfo.cxx_is_sun_studio?
		fPIC = "-KPIC"
	else
		fPIC = "-fPIC"
	end
	run_compiler "#{CXX} #{shlib_flag} #{sources} #{fPIC} -o #{target} #{flags}"
end
