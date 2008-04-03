#  Phusion Passenger - http://www.modrails.com/
#  Copyright (C) 2008  Phusion
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

# Rake functions for compiling/linking C++ stuff.

def compile_c(source, flags = CXXFLAGS)
	sh "#{CXX} #{flags} -c #{source}"
end

def compile_cxx(source, flags = CXXFLAGS)
	sh "#{CXX} #{flags} -c #{source}"
end

def create_static_library(target, sources)
	sh "ar cru #{target} #{sources}"
	sh "ranlib #{target}"
end

def create_executable(target, sources, linkflags = LDFLAGS)
	sh "#{CXX} #{sources} -o #{target} #{linkflags}"
end

def create_shared_library(target, sources, flags = LDFLAGS)
	if RUBY_PLATFORM =~ /darwin/
		shlib_flag = "-flat_namespace -bundle -undefined dynamic_lookup"
	else
		shlib_flag = "-shared"
	end
	sh "#{CXX} #{shlib_flag} #{sources} -fPIC -o #{target} #{flags}"
end
