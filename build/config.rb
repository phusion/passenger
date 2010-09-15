#  Phusion Passenger - http://www.modrails.com/
#  Copyright (C) 2010  Phusion
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; version 2 of the License.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License along
#  with this program; if not, write to the Free Software Foundation, Inc.,
#  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

OPTIMIZE = boolean_option("OPTIMIZE")
CC       = string_option("CC", "gcc")
CXX      = string_option("CXX", "g++")
if OPTIMIZE
	OPTIMIZATION_FLAGS = "#{PlatformInfo.debugging_cflags} -O2 -DBOOST_DISABLE_ASSERTS".strip
else
	OPTIMIZATION_FLAGS = "#{PlatformInfo.debugging_cflags} -DPASSENGER_DEBUG -DBOOST_DISABLE_ASSERTS".strip
end

# Extra compiler flags that should always be passed to the C/C++ compiler.
# Should be included last in the command string, even after PlatformInfo.portability_cflags.
EXTRA_CXXFLAGS = "-Wall #{OPTIMIZATION_FLAGS}".strip

# Extra linker flags that should always be passed to the linker.
# Should be included last in the command string, even after PlatformInfo.portability_ldflags.
EXTRA_LDFLAGS  = ""

# Whether to use the vendored libev or the system one.
USE_VENDORED_LIBEV = boolean_option("USE_VENDORED_LIBEV", true)