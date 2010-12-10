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

require 'rbconfig'
require 'phusion_passenger/platform_info'

module PhusionPassenger

module PlatformInfo
	# Returns the operating system's name. This name is in lowercase and contains no spaces,
	# and thus is suitable to be used in some kind of ID. E.g. "linux", "macosx".
	def self.os_name
		if Config::CONFIG['target_os'] =~ /darwin/ && (sw_vers = find_command('sw_vers'))
			return "macosx"
		else
			return RUBY_PLATFORM.sub(/.*?-/, '')
		end
	end
	memoize :os_name
	
	# The current platform's shared library extension ('so' on most Unices).
	def self.library_extension
		if RUBY_PLATFORM =~ /darwin/
			return "bundle"
		else
			return "so"
		end
	end
	
	# Returns a list of all CPU architecture names that the current machine CPU
	# supports. If there are multiple such architectures then the first item in
	# the result denotes that OS runtime's main/preferred architecture.
	#
	# This function normalizes some names. For example x86 is always reported
	# as "x86" regardless of whether the OS reports it as "i386" or "i686".
	# x86_64 is always reported as "x86_64" even if the OS reports it as "amd64".
	#
	# Please note that even if the CPU supports multiple architectures, the
	# operating system might not. For example most x86 CPUs nowadays also
	# support x86_64, but x86_64 Linux systems require various x86 compatibility
	# libraries to be installed before x86 executables can be run. This function
	# does not detect whether these compatibility libraries are installed.
	# The only guarantee that you have is that the OS can run executables in
	# the architecture denoted by the first item in the result.
	#
	# For example, on x86_64 Linux this function can return ["x86_64", "x86"].
	# This indicates that the CPU supports both of these architectures, and that
	# the OS's main/preferred architecture is x86_64. Most executables on the
	# system are thus be x86_64. It is guaranteed that the OS can run x86_64
	# executables, but not x86 executables per se.
	#
	# Another example: on MacOS X this function can return either
	# ["x86_64", "x86"] or ["x86", "x86_64"]. The former result indicates
	# OS X 10.6 (Snow Leopard) and beyond because starting from that version
	# everything is 64-bit by default. The latter result indicates an OS X
	# version older than 10.6.
	def self.cpu_architectures
		if os_name == "macosx"
			arch = `uname -p`.strip
			if arch == "i386"
				# Macs have been x86 since around 2007. I think all of them come with
				# a recent enough Intel CPU that supports both x86 and x86_64, and I
				# think every OS X version has both the x86 and x86_64 runtime installed.
				major, minor, *rest = `sw_vers -productVersion`.strip.split(".")
				major = major.to_i
				minor = minor.to_i
				if major >= 10 || (major == 10 && minor >= 6)
					# Since Snow Leopard x86_64 is the default.
					["x86_64", "x86"]
				else
					# Before Snow Leopard x86 was the default.
					["x86", "x86_64"]
				end
			else
				arch
			end
		else
			arch = `uname -p`.strip
			# On some systems 'uname -p' returns something like
			# 'Intel(R) Pentium(R) M processor 1400MHz'.
			if arch == "unknown" || arch =~ / /
				arch = `uname -m`.strip
			end
			if arch =~ /^i.86$/
				arch = "x86"
			elsif arch == "amd64"
				arch = "x86_64"
			end
			
			if arch == "x86"
				# Most x86 operating systems nowadays are probably running on
				# a CPU that supports both x86 and x86_64, but we're not gonna
				# go through the trouble of checking that. The main architecture
				# is what we usually care about.
				["x86"]
			elsif arch == "x86_64"
				# I don't think there's a single x86_64 CPU out there
				# that doesn't support x86 as well.
				["x86_64", "x86"]
			else
				[arch]
			end
		end
	end
	memoize :cpu_architectures, true
	
	# Returns whether the OS's main CPU architecture supports the
	# x86/x86_64 sfence instruction.
	def self.supports_sfence_instruction?
		arch = cpu_architectures[0]
		return arch == "x86_64" || (arch == "x86" &&
			try_compile_and_run(:c, %Q{
				int
				main() {
					__asm__ __volatile__ ("sfence" ::: "memory");
					return 0;
				}
			}))
	end
	memoize :supports_sfence_instruction?, true
	
	# Returns whether the OS's main CPU architecture supports the
	# x86/x86_64 lfence instruction.
	def self.supports_lfence_instruction?
		arch = cpu_architectures[0]
		return arch == "x86_64" || (arch == "x86" &&
			try_compile_and_run(:c, %Q{
				int
				main() {
					__asm__ __volatile__ ("lfence" ::: "memory");
					return 0;
				}
			}))
	end
	memoize :supports_lfence_instruction?, true
end

end # module PhusionPassenger
