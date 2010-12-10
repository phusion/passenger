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
require 'phusion_passenger/platform_info/ruby'
require 'phusion_passenger/platform_info/operating_system'

module PhusionPassenger

module PlatformInfo
	# Returns an array of identifiers that describe the current Ruby
	# interpreter's extension binary compatibility. A Ruby extension
	# compiled for a certain Ruby interpreter can also be loaded on
	# a different Ruby interpreter with the same binary compatibility
	# identifiers.
	#
	# The identifiers depend on the following factors:
	# - Ruby engine name.
	# - Ruby extension version.
	#   This is not the same as the Ruby language version, which
	#   identifies language-level compatibility. This is rather about
	#   binary compatibility of extensions.
	#   MRI seems to break source compatibility between tiny releases,
	#   though patchlevel releases tend to be source and binary
	#   compatible.
	# - Ruby extension architecture.
	#   This is not necessarily the same as the operating system
	#   runtime architecture or the CPU architecture.
	#   For example, in case of JRuby, the extension architecture is
	#   just "java" because all extensions target the Java platform;
	#   the architecture the JVM was compiled for has no effect on
	#   compatibility.
	#   On systems with universal binaries support there may be multiple
	#   architectures. In this case the architecture is "universal"
	#   because extensions must be able to support all of the Ruby
	#   executable's architectures.
	# - The operating system for which the Ruby interpreter was compiled.
	def self.ruby_extension_binary_compatibility_ids
		ruby_engine = defined?(RUBY_ENGINE) ? RUBY_ENGINE : "ruby"
		ruby_ext_version = RUBY_VERSION
		if RUBY_PLATFORM =~ /darwin/
			if RUBY_PLATFORM =~ /universal/
				ruby_arch = "universal"
			else
				# Something like:
				# "/opt/ruby-enterprise/bin/ruby: Mach-O 64-bit executable x86_64"
				ruby_arch = `file -L "#{ruby_executable}"`.strip
				ruby_arch.sub!(/.* /, '')
			end
		elsif RUBY_PLATFORM == "java"
			ruby_arch = "java"
		else
			ruby_arch = cpu_architectures[0]
		end
		return [ruby_engine, ruby_ext_version, ruby_arch, os_name]
	end
	memoize :ruby_extension_binary_compatibility_ids
	
	# Returns an identifier string that describes the current
	# platform's binary compatibility with regard to Phusion Passenger
	# binaries, both the Ruby extension and the C++ binaries. Two
	# systems with the same binary compatibility identifiers
	# are able to run the same Phusion Passenger binaries.
	#
	# The the string depends on the following factors:
	# - The Ruby extension binary compatibility identifiers.
	# - The operating system name.
	# - Operating system runtime identifier.
	#   This may include the kernel version, libc version, C++ ABI version,
	#   etc. Everything that is of interest for binary compatibility with
	#   Phusion Passenger's C++ components.
	# - Operating system default runtime architecture.
	#   This is not the same as the CPU architecture; some CPUs support
	#   multiple architectures, e.g. Intel Core 2 Duo supports x86 and
	#   x86_64. Some operating systems actually support multiple runtime
	#   architectures: a lot of x86_64 Linux distributions also include
	#   32-bit runtimes, and OS X Snow Leopard is x86_64 by default but
	#   all system libraries also support x86.
	#   This component identifies the architecture that is used when
	#   compiling a binary with the system's C++ compiler with its default
	#   options.
	def self.passenger_binary_compatibility_id
		ruby_engine, ruby_ext_version, ruby_arch, os_name =
			ruby_extension_binary_compatibility_ids
		
		if os_name == "macosx"
			# RUBY_PLATFORM gives us the kernel version, but we want
			# the OS X version.
			os_version_string = `sw_vers -productVersion`.strip
			# sw_vers returns something like "10.6.2". We're only
			# interested in the first two digits (MAJOR.MINOR) since
			# tiny releases tend to be binary compatible with each
			# other.
			components = os_version_string.split(".")
			os_version = "#{components[0]}.#{components[1]}"
			os_runtime = os_version
			
			os_arch = cpu_architectures[0]
			if os_version >= "10.5" && os_arch =~ /^i.86$/
				# On Snow Leopard, 'uname -m' returns i386 but
				# we *know* that everything is x86_64 by default.
				os_arch = "x86_64"
			end
		else
			os_arch = cpu_architectures[0]
			
			cpp = find_command('cpp')
			if cpp
				macros = `#{cpp} -dM < /dev/null`
				
				# Can be something like "4.3.2"
				# or "4.2.1 20070719 (FreeBSD)"
				macros =~ /__VERSION__ "(.+)"/
				compiler_version = $1
				compiler_version.gsub!(/ .*/, '') if compiler_version
				
				macros =~ /__GXX_ABI_VERSION (.+)$/
				cxx_abi_version = $1
			else
				compiler_version = nil
				cxx_abi_version = nil
			end
			
			if compiler_version && cxx_abi_version
				os_runtime = "gcc#{compiler_version}-#{cxx_abi_version}"
			else
				os_runtime = [compiler_version, cxx_abi_version].compact.join("-")
				if os_runtime.empty?
					os_runtime = `uname -r`.strip
				end
			end
		end
		
		if ruby_engine == "jruby"
			# For JRuby it's kinda useless to prepend "java" as extension
			# architecture because JRuby doesn't allow any other extension
			# architecture.
			identifier = ""
		else
			identifier = "#{ruby_arch}-"
		end
		identifier << "#{ruby_engine}#{ruby_ext_version}-"
		# If the extension architecture is the same as the OS architecture
		# then there's no need to specify it twice.
		if ruby_arch != os_arch
			identifier << "#{os_arch}-"
		end
		identifier << "#{os_name}-#{os_runtime}"
		return identifier
	end
	memoize :passenger_binary_compatibility_id
end

end # module PhusionPassenger
