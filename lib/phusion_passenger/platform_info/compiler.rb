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

require 'phusion_passenger/platform_info'
require 'phusion_passenger/platform_info/operating_system'

module PhusionPassenger

module PlatformInfo
	def self.gnu_make
		gmake = find_command('gmake')
		if !gmake
			gmake = find_command('make')
			if gmake
				if `#{gmake} --version 2>&1` =~ /GNU/
					return gmake
				else
					return nil
				end
			else
				return nil
			end
		else
			return gmake
		end
	end
	memoize :gnu_make, true
	
	# Checks whether the compiler supports "-arch #{arch}".
	def self.compiler_supports_architecture?(arch)
		return try_compile(:c, '', "-arch #{arch}")
	end
	
	def self.compiler_supports_visibility_flag?
		return try_compile(:c, '', '-fvisibility=hidden')
	end
	memoize :compiler_supports_visibility_flag?, true
	
	def self.compiler_supports_wno_attributes_flag?
		return try_compile(:c, '', '-Wno-attributes')
	end
	memoize :compiler_supports_wno_attributes_flag?, true
	
	def self.compiler_supports_wno_missing_field_initializers_flag?
		return try_compile(:c, '', '-Wno-missing-field-initializers')
	end
	memoize :compiler_supports_wno_missing_field_initializers_flag?
	
	def self.compiler_supports_no_tls_direct_seg_refs_option?
		return try_compile(:c, '', '-mno-tls-direct-seg-refs')
	end
	memoize :compiler_supports_no_tls_direct_seg_refs_option?, true
	
	# Returns whether compiling C++ with -fvisibility=hidden might result
	# in tons of useless warnings, like this:
	# http://code.google.com/p/phusion-passenger/issues/detail?id=526
	# This appears to be a bug in older g++ versions:
	# http://gcc.gnu.org/ml/gcc-patches/2006-07/msg00861.html
	# Warnings should be suppressed with -Wno-attributes.
	def self.compiler_visibility_flag_generates_warnings?
		if RUBY_PLATFORM =~ /linux/ && `#{cxx} -v 2>&1` =~ /gcc version (.*?)/
			return $1 <= "4.1.2"
		else
			return false
		end
	end
	memoize :compiler_visibility_flag_generates_warnings?, true
	
	def self.has_math_library?
		return try_link(:c, "int main() { return 0; }\n", '-lmath')
	end
	memoize :has_math_library?, true
	
	def self.has_alloca_h?
		return try_compile(:c, '#include <alloca.h>')
	end
	memoize :has_alloca_h?, true

	# Compiler flags that should be used for compiling every C/C++ program,
	# for portability reasons. These flags should be specified as last
	# when invoking the compiler.
	def self.portability_cflags
		flags = ["-D_REENTRANT -I/usr/local/include"]
		
		# Google SparseHash flags.
		# Figure out header for hash function object and its namespace.
		# Based on stl_hash.m4 and stl_hash_fun.m4 in the Google SparseHash sources.
		hash_namespace = nil
		ok = false
		['__gnu_cxx', '', 'std', 'stdext'].each do |namespace|
			['ext/hash_map', 'hash_map'].each do |hash_map_header|
				ok = try_compile(:cxx, %Q{
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
				end
			end
			break if ok
		end
		['ext/hash_fun.h', 'functional', 'tr1/functional',
		 'ext/stl_hash_fun.h', 'hash_fun.h', 'stl_hash_fun.h',
		 'stl/_hash_fun.h'].each do |hash_function_header|
			ok = try_compile(:cxx, %Q{
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
		
		if RUBY_PLATFORM =~ /solaris/
			flags << '-pthreads'
			if RUBY_PLATFORM =~ /solaris2.11/
				# skip the _XOPEN_SOURCE and _XPG4_2 definitions in later versions of Solaris / OpenIndiana
				flags << '-D__EXTENSIONS__ -D__SOLARIS__ -D_FILE_OFFSET_BITS=64'
			else
				flags << '-D_XOPEN_SOURCE=500 -D_XPG4_2 -D__EXTENSIONS__ -D__SOLARIS__ -D_FILE_OFFSET_BITS=64'
				flags << '-D__SOLARIS9__ -DBOOST__STDC_CONSTANT_MACROS_DEFINED' if RUBY_PLATFORM =~ /solaris2.9/
			end
			flags << '-DBOOST_HAS_STDINT_H' unless RUBY_PLATFORM =~ /solaris2.9/
			flags << '-mcpu=ultrasparc' if RUBY_PLATFORM =~ /sparc/
		elsif RUBY_PLATFORM =~ /openbsd/
			flags << '-DBOOST_HAS_STDINT_H -D_GLIBCPP__PTHREADS'
		elsif RUBY_PLATFORM =~ /aix/
			flags << '-DOXT_DISABLE_BACKTRACES'
		elsif RUBY_PLATFORM =~ /(sparc-linux|arm-linux|^arm.*-linux|sh4-linux)/
			# http://code.google.com/p/phusion-passenger/issues/detail?id=200
			# http://groups.google.com/group/phusion-passenger/t/6b904a962ee28e5c
			# http://groups.google.com/group/phusion-passenger/browse_thread/thread/aad4bd9d8d200561
			flags << '-DBOOST_SP_USE_PTHREADS'
		end
		
		flags << '-DHAS_ALLOCA_H_' if has_alloca_h?
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
	
	# C compiler flags that should be passed in order to enable debugging information.
	def self.debugging_cflags
		if RUBY_PLATFORM =~ /openbsd/
			# According to OpenBSD's pthreads man page, pthreads do not work
			# correctly when an app is compiled with -g. It recommends using
			# -ggdb instead.
			return '-ggdb'
		else
			return '-g'
		end
	end
	
	def self.export_dynamic_flags
		if RUBY_PLATFORM =~ /linux/
			return '-rdynamic'
		else
			return nil
		end
	end
end

end # module PhusionPassenger
