#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2010-2013 Phusion
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

begin
	require 'rubygems'
rescue LoadError
end
require 'pathname'
require 'fileutils'
require 'phusion_passenger'
PhusionPassenger.locate_directories
PhusionPassenger.require_passenger_lib 'packaging'
PhusionPassenger.require_passenger_lib 'platform_info'
PhusionPassenger.require_passenger_lib 'platform_info/operating_system'
PhusionPassenger.require_passenger_lib 'platform_info/binary_compatibility'
PhusionPassenger.require_passenger_lib 'platform_info/ruby'
PhusionPassenger.require_passenger_lib 'platform_info/apache'
PhusionPassenger.require_passenger_lib 'platform_info/curl'
PhusionPassenger.require_passenger_lib 'platform_info/zlib'
PhusionPassenger.require_passenger_lib 'platform_info/compiler'
PhusionPassenger.require_passenger_lib 'platform_info/cxx_portability'

include PhusionPassenger
include PhusionPassenger::PlatformInfo

require 'build/rake_extensions'
require 'build/cplusplus_support'

#################################################

class TemplateRenderer
	def initialize(filename)
		require 'erb' if !defined?(ERB)
		@erb = ERB.new(File.read(filename), nil, "-")
		@erb.filename = filename
	end

	def render
		return @erb.result(binding)
	end

	def render_to(filename)
		puts "Creating #{filename}"
		text = render
		# When packaging, some timestamps may be modified. The user may not
		# have write access to the source root (for example, when Passenger
		# Standalone is compiling its runtime), so we only write to the file
		# when necessary.
		if !File.exist?(filename) || File.writable?(filename) || File.read(filename) != text
			File.open(filename, 'w') do |f|
				f.write(text)
			end
		end
	end
end

def string_option(name, default_value = nil)
	value = ENV[name]
	if value.nil? || value.empty?
		return default_value
	else
		return value
	end
end

def compiler_flag_option(name)
	return string_option(name, '').gsub("\n", " ")
end

def boolean_option(name, default_value = false)
	value = ENV[name]
	if value.nil? || value.empty?
		return default_value
	else
		return value == "yes" || value == "on" || value == "true" || value == "1"
	end
end

def maybe_wrap_in_ccache(command)
	if boolean_option('USE_CCACHE', false) && command !~ /^ccache /
		return "ccache #{command}"
	else
		return command
	end
end

#################################################

if string_option('OUTPUT_DIR')
	OUTPUT_DIR = string_option('OUTPUT_DIR') + "/"
else
	OUTPUT_DIR = "buildout/"
end

verbose true if !boolean_option('REALLY_QUIET')
if boolean_option('STDERR_TO_STDOUT')
	# Just redirecting the file descriptor isn't enough because
	# data written to STDERR might arrive in an unexpected order
	# compared to STDOUT.
	STDERR.reopen(STDOUT)
	Object.send(:remove_const, :STDERR)
	STDERR = STDOUT
	$stderr = $stdout
end

if boolean_option('CACHING', true) && !boolean_option('RELEASE')
	PlatformInfo.cache_dir = OUTPUT_DIR + "cache"
	FileUtils.mkdir_p(PlatformInfo.cache_dir)
end

# https://github.com/phusion/passenger/issues/672
ENV.delete('CDPATH')

#################################################

PACKAGE_NAME    = PhusionPassenger::PACKAGE_NAME
PACKAGE_VERSION = PhusionPassenger::VERSION_STRING
MAINTAINER_NAME  = "Phusion"
MAINTAINER_EMAIL = "info@phusion.nl"

OPTIMIZE = boolean_option("OPTIMIZE")
CC       = maybe_wrap_in_ccache(PhusionPassenger::PlatformInfo.cc)
CXX      = maybe_wrap_in_ccache(PhusionPassenger::PlatformInfo.cxx)
LIBEXT   = PlatformInfo.library_extension
USE_DMALLOC = boolean_option('USE_DMALLOC')
USE_EFENCE  = boolean_option('USE_EFENCE')
USE_ASAN    = boolean_option('USE_ASAN')

# Agent-specific compiler flags.
AGENT_CFLAGS  = ""
AGENT_CFLAGS << " #{PlatformInfo.adress_sanitizer_flag}" if USE_ASAN
AGENT_CFLAGS.strip!

# Agent-specific linker flags.
AGENT_LDFLAGS = ""
AGENT_LDFLAGS << " #{PlatformInfo.dmalloc_ldflags}" if USE_DMALLOC
AGENT_LDFLAGS << " #{PlatformInfo.electric_fence_ldflags}" if USE_EFENCE
AGENT_LDFLAGS << " #{PlatformInfo.adress_sanitizer_flag}" if USE_ASAN
# Extra linker flags for backtrace_symbols() to generate useful output (see AgentsBase.cpp).
AGENT_LDFLAGS << " #{PlatformInfo.export_dynamic_flags}"
# Enable dead symbol elimination on OS X.
AGENT_LDFLAGS << " -Wl,-dead_strip" if PlatformInfo.os_name == "macosx"
AGENT_LDFLAGS.strip!

# Extra compiler flags that should always be passed to the C/C++ compiler.
# These should be included first in the command string, before anything else.
EXTRA_PRE_CFLAGS = compiler_flag_option('EXTRA_PRE_CFLAGS')
EXTRA_PRE_CXXFLAGS = compiler_flag_option('EXTRA_PRE_CXXFLAGS')
# These should be included last in the command string.
EXTRA_CFLAGS = PlatformInfo.default_extra_cflags.dup
EXTRA_CFLAGS << " " << compiler_flag_option('EXTRA_CFLAGS') if !compiler_flag_option('EXTRA_CFLAGS').empty?
EXTRA_CXXFLAGS = PlatformInfo.default_extra_cxxflags.dup
EXTRA_CXXFLAGS << " " << compiler_flag_option('EXTRA_CXXFLAGS') if !compiler_flag_option('EXTRA_CXXFLAGS').empty?
[EXTRA_CFLAGS, EXTRA_CXXFLAGS].each do |flags|
	flags << " -fno-omit-frame-pointers" if USE_ASAN
	flags << " -DPASSENGER_DISABLE_THREAD_LOCAL_STORAGE" if !boolean_option('PASSENGER_THREAD_LOCAL_STORAGE', true)
end

# Extra linker flags that should always be passed to the linker.
# These should be included first in the command string.
EXTRA_PRE_C_LDFLAGS   = compiler_flag_option('EXTRA_PRE_LDFLAGS') + " " +
	compiler_flag_option('EXTRA_PRE_C_LDFLAGS')
EXTRA_PRE_CXX_LDFLAGS = compiler_flag_option('EXTRA_PRE_LDFLAGS') + " " +
	compiler_flag_option('EXTRA_PRE_CXX_LDFLAGS')
# These should be included last in the command string, even after portability_*_ldflags.
EXTRA_C_LDFLAGS   = compiler_flag_option('EXTRA_LDFLAGS') + " " +
	compiler_flag_option('EXTRA_C_LDFLAGS')
EXTRA_CXX_LDFLAGS = compiler_flag_option('EXTRA_LDFLAGS') + " " +
	compiler_flag_option('EXTRA_CXX_LDFLAGS')


AGENT_OUTPUT_DIR          = string_option('AGENT_OUTPUT_DIR', OUTPUT_DIR + "agents") + "/"
COMMON_OUTPUT_DIR         = string_option('COMMON_OUTPUT_DIR', OUTPUT_DIR + "common") + "/"
APACHE2_OUTPUT_DIR        = string_option('APACHE2_OUTPUT_DIR', OUTPUT_DIR + "apache2") + "/"
LIBEV_OUTPUT_DIR          = string_option('LIBEV_OUTPUT_DIR', OUTPUT_DIR + "libev") + "/"
LIBEIO_OUTPUT_DIR         = string_option('LIBEIO_OUTPUT_DIR', OUTPUT_DIR + "libeio") + "/"
ruby_extension_archdir    = PlatformInfo.ruby_extension_binary_compatibility_id
RUBY_EXTENSION_OUTPUT_DIR = string_option('RUBY_EXTENSION_OUTPUT_DIR',
	OUTPUT_DIR + "ruby/" + ruby_extension_archdir) + "/"
PKG_DIR                   = string_option('PKG_DIR', "pkg")


# Whether to use the vendored libev or the system one.
USE_VENDORED_LIBEV = boolean_option("USE_VENDORED_LIBEV", true)
# Whether to use the vendored libeio or the system one.
USE_VENDORED_LIBEIO = boolean_option("USE_VENDORED_LIBEIO", true)
