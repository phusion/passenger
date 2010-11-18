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

require 'rubygems'
require 'pathname'
require 'phusion_passenger'
require 'phusion_passenger/packaging'
require 'phusion_passenger/platform_info'
require 'phusion_passenger/platform_info/operating_system'
require 'phusion_passenger/platform_info/binary_compatibility'
require 'phusion_passenger/platform_info/ruby'
require 'phusion_passenger/platform_info/apache'
require 'phusion_passenger/platform_info/curl'
require 'phusion_passenger/platform_info/zlib'
require 'phusion_passenger/platform_info/compiler'
require 'phusion_passenger/platform_info/documentation_tools'

include PhusionPassenger
include PhusionPassenger::PlatformInfo

require 'build/rdoctask'
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

if string_option('OUTPUT_DIR')
	OUTPUT_DIR = string_option('OUTPUT_DIR') + "/"
else
	OUTPUT_DIR = ""
end
AGENT_OUTPUT_DIR          = string_option('AGENT_OUTPUT_DIR', OUTPUT_DIR + "agents") + "/"
COMMON_OUTPUT_DIR         = string_option('COMMON_OUTPUT_DIR', OUTPUT_DIR + "ext/common") + "/"
APACHE2_OUTPUT_DIR        = string_option('APACHE2_OUTPUT_DIR', OUTPUT_DIR + "ext/apache2") + "/"
LIBEV_OUTPUT_DIR          = string_option('LIBEV_OUTPUT_DIR', OUTPUT_DIR + "ext/libev") + "/"
ruby_extension_archdir = PlatformInfo.ruby_extension_binary_compatibility_ids.join("-")
RUBY_EXTENSION_OUTPUT_DIR = string_option('RUBY_EXTENSION_OUTPUT_DIR',
	OUTPUT_DIR + "ext/ruby/" + ruby_extension_archdir) + "/"

LIBEXT = PlatformInfo.library_extension

# Extra linker flags for backtrace_symbols() to generate useful output (see AgentsBase.cpp).
AGENT_LDFLAGS = PlatformInfo.export_dynamic_flags


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
