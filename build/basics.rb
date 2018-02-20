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

begin
  require 'rubygems'
rescue LoadError
end
require 'fileutils'
require 'phusion_passenger'
PhusionPassenger.locate_directories
PhusionPassenger.require_passenger_lib 'constants'
PhusionPassenger.require_passenger_lib 'packaging'
PhusionPassenger.require_passenger_lib 'utils/shellwords'
PhusionPassenger.require_passenger_lib 'platform_info'
PhusionPassenger.require_passenger_lib 'platform_info/operating_system'
PhusionPassenger.require_passenger_lib 'platform_info/binary_compatibility'
PhusionPassenger.require_passenger_lib 'platform_info/ruby'
PhusionPassenger.require_passenger_lib 'platform_info/apache'
PhusionPassenger.require_passenger_lib 'platform_info/curl'
PhusionPassenger.require_passenger_lib 'platform_info/zlib'
PhusionPassenger.require_passenger_lib 'platform_info/crypto'
PhusionPassenger.require_passenger_lib 'platform_info/openssl'
PhusionPassenger.require_passenger_lib 'platform_info/compiler'
PhusionPassenger.require_passenger_lib 'platform_info/cxx_portability'

PlatformInfo = PhusionPassenger::PlatformInfo
SHORT_PROGRAM_NAME = PhusionPassenger::SHORT_PROGRAM_NAME
PhusionPassenger::SharedConstants.constants.each do |name|
  Kernel.const_set(name, PhusionPassenger::SharedConstants.const_get(name))
end

if File.exist?("#{SOURCE_ROOT}/build/support/cxx_dependency_map.rb")
  require_build_system_file 'support/cxx_dependency_map'
else
  CXX_DEPENDENCY_MAP = {}
end
require_build_system_file 'support/general'
require_build_system_file 'support/cplusplus'

if string_option('OUTPUT_DIR')
  OUTPUT_DIR = string_option('OUTPUT_DIR') + "/"
else
  OUTPUT_DIR = "buildout/"
end

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

let(:cc) { maybe_wrap_in_ccache(PhusionPassenger::PlatformInfo.cc) }
let(:cxx) { maybe_wrap_in_ccache(PhusionPassenger::PlatformInfo.cxx) }
let(:libext) { PlatformInfo.library_extension }

USE_ASAN    = boolean_option('USE_ASAN')
USE_SELINUX = boolean_option('USE_SELINUX')
OPTIMIZE    = boolean_option('OPTIMIZE')
LTO         = OPTIMIZE && boolean_option('LTO')

CXX_SUPPORTLIB_INCLUDE_PATHS = [
  "src/cxx_supportlib",
  "src/cxx_supportlib/vendor-copy",
  "src/cxx_supportlib/vendor-modified"
]

# Extra compiler flags that should always be passed to the C/C++ compiler.
# These should be included first in the command string, before anything else.
EXTRA_PRE_CFLAGS = compiler_flag_option('EXTRA_PRE_CFLAGS')
EXTRA_PRE_CXXFLAGS = compiler_flag_option('EXTRA_PRE_CXXFLAGS')
# These should be included last in the command string.
let(:extra_cflags) do
  result = PlatformInfo.default_extra_cflags.dup
  result << " " << compiler_flag_option('EXTRA_CFLAGS') if !compiler_flag_option('EXTRA_CFLAGS').empty?
  result << " #{PlatformInfo.address_sanitizer_flag}" if USE_ASAN && PlatformInfo.address_sanitizer_flag
  result << " -fno-omit-frame-pointer" if USE_ASAN
  result << " -DPASSENGER_DISABLE_THREAD_LOCAL_STORAGE" if !boolean_option('PASSENGER_THREAD_LOCAL_STORAGE', true)
  result
end
let(:extra_cxxflags) do
  result = PlatformInfo.default_extra_cxxflags.dup
  result << " " << compiler_flag_option('EXTRA_CXXFLAGS') if !compiler_flag_option('EXTRA_CXXFLAGS').empty?
  result << " #{PlatformInfo.address_sanitizer_flag}" if USE_ASAN && PlatformInfo.address_sanitizer_flag
  result << " -fno-omit-frame-pointer" if USE_ASAN
  result << " -DPASSENGER_DISABLE_THREAD_LOCAL_STORAGE" if !boolean_option('PASSENGER_THREAD_LOCAL_STORAGE', true)
  result
end

# Extra linker flags that should always be passed to the linker.
# These should be included first in the command string.
EXTRA_PRE_C_LDFLAGS   = compiler_flag_option('EXTRA_PRE_LDFLAGS') + " " +
  compiler_flag_option('EXTRA_PRE_C_LDFLAGS')
EXTRA_PRE_CXX_LDFLAGS = compiler_flag_option('EXTRA_PRE_LDFLAGS') + " " +
  compiler_flag_option('EXTRA_PRE_CXX_LDFLAGS')
# These should be included last in the command string, even after portability_*_ldflags.
let(:extra_c_ldflags) do
  result = []
  result << PlatformInfo.address_sanitizer_flag if USE_ASAN
  result << compiler_flag_option('EXTRA_LDFLAGS')
  result << compiler_flag_option('EXTRA_C_LDFLAGS')
  result.compact!
  result.join(' ')
end
let(:extra_cxx_ldflags) do
  result = []
  result << PlatformInfo.address_sanitizer_flag if USE_ASAN
  result << compiler_flag_option('EXTRA_LDFLAGS')
  result << compiler_flag_option('EXTRA_CXX_LDFLAGS')
  result.compact!
  result.join(' ')
end


AGENT_OUTPUT_DIR          = string_option('AGENT_OUTPUT_DIR', OUTPUT_DIR + "support-binaries") + "/"
COMMON_OUTPUT_DIR         = string_option('COMMON_OUTPUT_DIR', OUTPUT_DIR + "common") + "/"
APACHE2_OUTPUT_DIR        = string_option('APACHE2_OUTPUT_DIR', OUTPUT_DIR + "apache2") + "/"
NGINX_DYNAMIC_OUTPUT_DIR  = string_option('NGINX_DYNAMIC_OUTPUT_DIR', OUTPUT_DIR + "nginx_dynamic") + "/"
LIBEV_OUTPUT_DIR          = string_option('LIBEV_OUTPUT_DIR', OUTPUT_DIR + "libev") + "/"
LIBUV_OUTPUT_DIR          = string_option('LIBUV_OUTPUT_DIR', OUTPUT_DIR + "libuv") + "/"
ruby_extension_archdir    = PlatformInfo.ruby_extension_binary_compatibility_id
RUBY_EXTENSION_OUTPUT_DIR = string_option('RUBY_EXTENSION_OUTPUT_DIR',
  OUTPUT_DIR + "ruby/" + ruby_extension_archdir) + "/"
PKG_DIR                   = string_option('PKG_DIR', "pkg")
TEST_OUTPUT_DIR           = string_option('TEST_OUTPUT_DIR', OUTPUT_DIR + "test") + "/"


# Whether to use the vendored libev or the system one.
USE_VENDORED_LIBEV = boolean_option("USE_VENDORED_LIBEV", true)
# Whether to use the vendored libuv or the system one.
USE_VENDORED_LIBUV  = boolean_option("USE_VENDORED_LIBUV", true)
