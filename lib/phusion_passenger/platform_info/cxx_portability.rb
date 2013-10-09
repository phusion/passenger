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

require 'phusion_passenger/platform_info'
require 'phusion_passenger/platform_info/compiler'
require 'phusion_passenger/platform_info/operating_system'

module PhusionPassenger

module PlatformInfo
	# Compiler flags that should be used for compiling every C/C++ program,
	# for portability reasons. These flags should be specified as last
	# when invoking the compiler.
	def self.portability_cflags
		flags = ["-D_REENTRANT -I/usr/local/include"]
		
		# There are too many implementations of of the hash map!
		# Figure out the right one.
		ok = try_compile("Checking for tr1/unordered_map", :cxx, %Q{
			#include <tr1/unordered_map>
			int
			main() {
				std::tr1::unordered_map<int, int> m;
				return 0;
			}
		})
		if ok
			flags << "-DHAS_TR1_UNORDERED_MAP"
		else
			hash_namespace = nil
			ok = false
			['__gnu_cxx', '', 'std', 'stdext'].each do |namespace|
				['hash_map', 'ext/hash_map'].each do |hash_map_header|
					ok = try_compile("Checking for #{hash_map_header}", :cxx, %Q{
						#include <#{hash_map_header}>
						int
						main() {
							#{namespace}::hash_map<int, int> m;
							return 0;
						}
					})
					if ok
						hash_namespace = namespace
						flags << "-DHASH_NAMESPACE=\"#{namespace}\""
						flags << "-DHASH_MAP_HEADER=\"<#{hash_map_header}>\""
						flags << "-DHASH_MAP_CLASS=\"hash_map\""
					end
					break if ok
				end
				break if ok
			end
			['ext/hash_fun.h', 'functional', 'tr1/functional',
			 'ext/stl_hash_fun.h', 'hash_fun.h', 'stl_hash_fun.h',
			 'stl/_hash_fun.h'].each do |hash_function_header|
				ok = try_compile("Checking for #{hash_function_header}", :cxx, %Q{
					#include <#{hash_function_header}>
					int
					main() {
						#{hash_namespace}::hash<int>()(5);
						return 0;
					}
				})
				if ok
					flags << "-DHASH_FUN_H=\"<#{hash_function_header}>\""
					break
				end
			end
		end

		ok = try_compile("Checking for accept4()", :c, %Q{
			#define _GNU_SOURCE
			#include <sys/socket.h>
			static void *foo = accept4;
		})
		flags << '-DHAVE_ACCEPT4' if ok
		
		if RUBY_PLATFORM =~ /solaris/
			if PhusionPassenger::PlatformInfo.cc_is_sun_studio?
				flags << '-mt'
			else
				flags << '-pthreads'
			end
			if RUBY_PLATFORM =~ /solaris2.11/
				# skip the _XOPEN_SOURCE and _XPG4_2 definitions in later versions of Solaris / OpenIndiana
				flags << '-D__EXTENSIONS__ -D__SOLARIS__ -D_FILE_OFFSET_BITS=64'
			else
				flags << '-D_XOPEN_SOURCE=500 -D_XPG4_2 -D__EXTENSIONS__ -D__SOLARIS__ -D_FILE_OFFSET_BITS=64'
				flags << '-D__SOLARIS9__ -DBOOST__STDC_CONSTANT_MACROS_DEFINED' if RUBY_PLATFORM =~ /solaris2.9/
			end
			flags << '-DBOOST_HAS_STDINT_H' unless RUBY_PLATFORM =~ /solaris2.9/
			if PhusionPassenger::PlatformInfo.cc_is_sun_studio?
				flags << '-xtarget=ultra' if RUBY_PLATFORM =~ /sparc/
			else
				flags << '-mcpu=ultrasparc' if RUBY_PLATFORM =~ /sparc/
			end
		elsif RUBY_PLATFORM =~ /openbsd/
			flags << '-DBOOST_HAS_STDINT_H -D_GLIBCPP__PTHREADS'
		elsif RUBY_PLATFORM =~ /aix/
			flags << '-pthread'
			flags << '-DOXT_DISABLE_BACKTRACES'
		elsif RUBY_PLATFORM =~ /(sparc-linux|arm-linux|^arm.*-linux|sh4-linux)/
			# http://code.google.com/p/phusion-passenger/issues/detail?id=200
			# http://groups.google.com/group/phusion-passenger/t/6b904a962ee28e5c
			# http://groups.google.com/group/phusion-passenger/browse_thread/thread/aad4bd9d8d200561
			flags << '-DBOOST_SP_USE_PTHREADS'
		end
		
		flags << '-DHAS_ALLOCA_H' if has_alloca_h?
		flags << '-DHAS_SFENCE' if supports_sfence_instruction?
		flags << '-DHAS_LFENCE' if supports_lfence_instruction?
		
		return flags.compact.join(" ").strip
	end
	memoize :portability_cflags, true

	# Linker flags that should be used for linking every C/C++ program,
	# for portability reasons. These flags should be specified as last
	# when invoking the linker.
	def self.portability_ldflags
		if RUBY_PLATFORM =~ /solaris/
			result = '-lxnet -lrt -lsocket -lnsl -lpthread'
		else
			result = '-lpthread'
		end
		flags << ' -lmath' if has_math_library?
		return result
	end
	memoize :portability_ldflags
end

end # module PhusionPassenger
