#  Phusion Passenger - http://www.modrails.com/
#  Copyright (c) 2010, 2011, 2012 Phusion
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

require 'rubygems'
require 'pathname'
require 'phusion_passenger'
PhusionPassenger.locate_directories
require 'phusion_passenger/packaging'
require 'phusion_passenger/platform_info'
require 'phusion_passenger/platform_info/operating_system'
require 'phusion_passenger/platform_info/binary_compatibility'
require 'phusion_passenger/platform_info/ruby'
require 'phusion_passenger/platform_info/apache'
require 'phusion_passenger/platform_info/curl'
require 'phusion_passenger/platform_info/zlib'
require 'phusion_passenger/platform_info/compiler'

include PhusionPassenger
include PhusionPassenger::PlatformInfo

require 'build/packagetask'
require 'build/gempackagetask'
require 'build/rake_extensions'
require 'build/cplusplus_support'

#################################################

def string_option(name, default_value = nil)
	value = ENV[name]
	if value.nil? || value.empty?
		return default_value
	else
		return value
	end
end

def boolean_option(name, default_value = false)
	value = ENV[name]
	if value.nil? || value.empty?
		return default_value
	else
		return value == "yes" || value == "on" || value == "true" || value == "1"
	end
end

#################################################

OPTIMIZE = boolean_option("OPTIMIZE")
CC       = string_option("CC", "gcc")
CXX      = string_option("CXX", "g++")
LIBEXT   = PlatformInfo.library_extension
USE_DMALLOC = boolean_option('USE_DMALLOC')
if OPTIMIZE
	OPTIMIZATION_FLAGS = "#{PlatformInfo.debugging_cflags} -O2 -DBOOST_DISABLE_ASSERTS".strip
else
	OPTIMIZATION_FLAGS = "#{PlatformInfo.debugging_cflags} -DPASSENGER_DEBUG -DBOOST_DISABLE_ASSERTS".strip
end
OPTIMIZATION_FLAGS << " -fcommon"
OPTIMIZATION_FLAGS << " -fvisibility=hidden -DVISIBILITY_ATTRIBUTE_SUPPORTED" if PlatformInfo.compiler_supports_visibility_flag?
OPTIMIZATION_FLAGS << " -Wno-attributes" if PlatformInfo.compiler_supports_visibility_flag? &&
	PlatformInfo.compiler_visibility_flag_generates_warnings? &&
	PlatformInfo.compiler_supports_wno_attributes_flag?

# Agent-specific compiler and linker flags.
AGENT_CFLAGS  = ""
AGENT_LDFLAGS = ""
AGENT_LDFLAGS << PlatformInfo.dmalloc_ldflags if USE_DMALLOC
AGENT_LDFLAGS << " -lmcheck" if RUBY_PLATFORM =~ /linux/
# Extra linker flags for backtrace_symbols() to generate useful output (see AgentsBase.cpp).
AGENT_LDFLAGS << " #{PlatformInfo.export_dynamic_flags}"
# Enable dead symbol elimination on OS X.
AGENT_LDFLAGS << " -Wl,-dead_strip" if RUBY_PLATFORM =~ /darwin/

# Extra compiler flags that should always be passed to the C/C++ compiler.
# Should be included last in the command string, even after PlatformInfo.portability_cflags.
EXTRA_CXXFLAGS = "-Wall -Wextra -Wno-unused-parameter -Wno-parentheses -Wpointer-arith -Wwrite-strings -Wno-long-long"
EXTRA_CXXFLAGS << " -Wno-missing-field-initializers" if PlatformInfo.compiler_supports_wno_missing_field_initializers_flag?
EXTRA_CXXFLAGS << " -mno-tls-direct-seg-refs" if PlatformInfo.requires_no_tls_direct_seg_refs? && PlatformInfo.compiler_supports_no_tls_direct_seg_refs_option?
# Work around Clang warnings in ev++.h.
EXTRA_CXXFLAGS << " -Wno-ambiguous-member-template" if PlatformInfo.cxx_is_clang?
EXTRA_CXXFLAGS << " #{OPTIMIZATION_FLAGS}" if !OPTIMIZATION_FLAGS.empty?

# Extra linker flags that should always be passed to the linker.
# Should be included last in the command string, even after PlatformInfo.portability_ldflags.
EXTRA_LDFLAGS  = ""


if string_option('OUTPUT_DIR')
	OUTPUT_DIR = string_option('OUTPUT_DIR') + "/"
else
	OUTPUT_DIR = ""
end
AGENT_OUTPUT_DIR          = string_option('AGENT_OUTPUT_DIR', OUTPUT_DIR + "agents") + "/"
COMMON_OUTPUT_DIR         = string_option('COMMON_OUTPUT_DIR', OUTPUT_DIR + "libout/common") + "/"
APACHE2_OUTPUT_DIR        = string_option('APACHE2_OUTPUT_DIR', OUTPUT_DIR + "libout/apache2") + "/"
LIBEV_OUTPUT_DIR          = string_option('LIBEV_OUTPUT_DIR', OUTPUT_DIR + "libout/libev") + "/"
LIBEIO_OUTPUT_DIR         = string_option('LIBEIO_OUTPUT_DIR', OUTPUT_DIR + "libout/libeio") + "/"
ruby_extension_archdir    = PlatformInfo.ruby_extension_binary_compatibility_id
RUBY_EXTENSION_OUTPUT_DIR = string_option('RUBY_EXTENSION_OUTPUT_DIR',
	OUTPUT_DIR + "libout/ruby/" + ruby_extension_archdir) + "/"


# Whether to use the vendored libev or the system one.
USE_VENDORED_LIBEV = boolean_option("USE_VENDORED_LIBEV", true)
# Whether to use the vendored libeio or the system one.
USE_VENDORED_LIBEIO = boolean_option("USE_VENDORED_LIBEIO", true)


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
	if OUTPUT_DIR.empty?
		PlatformInfo.cache_dir = File.expand_path("cache", File.dirname(__FILE__))
	else
		PlatformInfo.cache_dir = OUTPUT_DIR + "cache"
	end
end
