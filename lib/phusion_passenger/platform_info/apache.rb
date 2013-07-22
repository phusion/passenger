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

module PhusionPassenger

# Wow, I can't believe in how many ways one can build Apache in OS
# X! We have to resort to all sorts of tricks to make Passenger build
# out of the box on OS X. :-(
#
# In the name of usability and the "end user is the king" line of thought,
# I shall suffer the horrible faith of writing tons of autodetection code!

# Users can change the detection behavior by setting the environment variable
# <tt>APXS2</tt> to the correct 'apxs' (or 'apxs2') binary, as provided by
# Apache.

module PlatformInfo
	################ Programs ################
	
	# The absolute path to the 'apxs' or 'apxs2' executable, or nil if not found.
	def self.apxs2
		if env_defined?("APXS2")
			return ENV["APXS2"]
		end
		['apxs2', 'apxs'].each do |name|
			command = find_command(name)
			if !command.nil?
				return command
			end
		end
		return nil
	end
	memoize :apxs2
	
	# The absolute path to the 'apachectl' or 'apache2ctl' binary, or nil if
	# not found.
	def self.apache2ctl(options = {})
		return find_apache2_executable('apache2ctl', 'apachectl2', 'apachectl', options)
	end
	memoize :apache2ctl
	
	# The absolute path to the Apache binary (that is, 'httpd', 'httpd2', 'apache'
	# or 'apache2'), or nil if not found.
	def self.httpd(options = {})
		apxs2 = options[:apxs2] || self.apxs2
		if env_defined?('HTTPD')
			return ENV['HTTPD']
		elsif apxs2.nil?
			["apache2", "httpd2", "apache", "httpd"].each do |name|
				command = find_command(name)
				if !command.nil?
					return command
				end
			end
			return nil
		else
			return find_apache2_executable(`#{apxs2} -q TARGET`.strip, options)
		end
	end
	memoize :httpd

	# The Apache version, or nil if Apache is not found.
	def self.httpd_version(options = nil)
		if options
			httpd = options[:httpd] || self.httpd(options)
		else
			httpd = self.httpd
		end
		if httpd
			`#{httpd} -v` =~ %r{Apache/([\d\.]+)}
			return $1
		else
			return nil
		end
	end
	memoize :httpd_version

	# The Apache root directory.
	def self.httpd_root(options = nil)
		if options
			httpd = options[:httpd] || self.httpd(options)
		else
			httpd = self.httpd
		end
		if httpd
			`#{httpd} -V` =~ / -D HTTPD_ROOT="(.+)"$/
			return $1
		else
			return nil
		end
	end
	memoize :httpd_root

	# The default Apache configuration file, or nil if Apache is not found.
	def self.httpd_default_config_file(options = nil)
		if options
			httpd = options[:httpd] || self.httpd(options)
		else
			httpd = self.httpd
		end
		if httpd
			info = `#{httpd} -V`
			info =~ /-D SERVER_CONFIG_FILE="(.+)"$/
			filename = $1
			if filename =~ /\A\//
				return filename
			else
				# Not an absolute path. Infer from root.
				if root = httpd_root(options)
					return "#{root}/#{filename}"
				else
					return nil
				end
			end
		else
			return nil
		end
	end
	memoize :httpd_default_config_file

	def self.httpd_default_error_log(options = nil)
		if options
			httpd = options[:httpd] || self.httpd(options)
		else
			httpd = self.httpd
		end
		if httpd
			info = `#{httpd} -V`
			info =~ /-D DEFAULT_ERRORLOG="(.+)"$/
			filename = $1
			if filename =~ /\A\//
				return filename
			else
				# Not an absolute path. Infer from root.
				if root = httpd_root(options)
					return "#{root}/#{filename}"
				else
					return nil
				end
			end
		else
			return nil
		end
	end
	memoize :httpd_default_error_log

	def self.httpd_actual_error_log(options = nil)
		if config_file = httpd_default_config_file(options)
			contents = File.read(config_file)
			# We don't want to match comments
			contents.gsub!(/^[ \t]*#.*/, '')
			if contents =~ /^ErrorLog (.+)$/
				return $1.strip.sub(/^"/, '').sub(/"$/, '')
			elsif contents =~ /ErrorLog/
				# The user apparently has ErrorLog set somewhere but
				# we can't parse it. The default error log location,
				# as reported by `httpd -V`, may be wrong (it is on OS X).
				# So to be safe, let's assume that we don't know.
				return nil
			else
				return httpd_default_error_log(options)
			end
		else
			return httpd_default_config_file(options)
		end
	end
	memoize :httpd_actual_error_log

	# Whether Apache appears to support a2enmod and a2dismod.
	def self.httpd_supports_a2enmod?(options = nil)
		config_file = httpd_default_config_file(options)
		if config_file
			config_dir = File.dirname(config_file)
			return File.exist?("#{config_dir}/mods-available") &&
				File.exist?("#{config_dir}/mods-enabled")
		else
			return nil
		end
	end

	# The absolute path to the 'a2enmod' executable.
	def self.a2enmod(options = {})
		apxs2 = options[:apxs2] || self.apxs2
		if env_defined?('A2ENMOD')
			return ENV['A2ENMOD']
		else
			return find_apache2_executable("a2enmod", options)
		end
	end
	memoize :a2enmod

	# The absolute path to the 'a2enmod' executable.
	def self.a2dismod(options = {})
		apxs2 = options[:apxs2] || self.apxs2
		if env_defined?('A2DISMOD')
			return ENV['A2DISMOD']
		else
			return find_apache2_executable("a2dismod", options)
		end
	end
	memoize :a2dismod
	
	# The absolute path to the 'apr-config' or 'apr-1-config' executable,
	# or nil if not found.
	def self.apr_config
		if env_defined?('APR_CONFIG')
			return ENV['APR_CONFIG']
		elsif apxs2.nil?
			return nil
		else
			filename = `#{apxs2} -q APR_CONFIG 2>/dev/null`.strip
			if filename.empty?
				apr_bindir = `#{apxs2} -q APR_BINDIR 2>/dev/null`.strip
				if apr_bindir.empty?
					return nil
				else
					return select_executable(apr_bindir,
						"apr-1-config", "apr-config")
				end
			elsif File.exist?(filename)
				return filename
			else
				return nil
			end
		end
	end
	memoize :apr_config
	
	# The absolute path to the 'apu-config' or 'apu-1-config' executable, or nil
	# if not found.
	def self.apu_config
		if env_defined?('APU_CONFIG')
			return ENV['APU_CONFIG']
		elsif apxs2.nil?
			return nil
		else
			filename = `#{apxs2} -q APU_CONFIG 2>/dev/null`.strip
			if filename.empty?
				apu_bindir = `#{apxs2} -q APU_BINDIR 2>/dev/null`.strip
				if apu_bindir.empty?
					return nil
				else
					return select_executable(apu_bindir,
						"apu-1-config", "apu-config")
				end
			elsif File.exist?(filename)
				return filename
			else
				return nil
			end
		end
	end
	memoize :apu_config

	# Find an executable in the Apache 'bin' and 'sbin' directories.
	# Returns nil if not found.
	def self.find_apache2_executable(*possible_names)
		if possible_names.last.is_a?(Hash)
			options = possible_names.pop
			options = nil if options.empty?
		end

		if options
			dirs = options[:dirs] || [apache2_bindir(options), apache2_sbindir(options)]
		else
			dirs = [apache2_bindir, apache2_sbindir]
		end

		dirs.each do |bindir|
			if bindir.nil?
				next
			end
			possible_names.each do |name|
				filename = "#{bindir}/#{name}"
				if !File.exist?(filename)
					log "Looking for #{filename}: not found"
				elsif !File.file?(filename)
					log "Looking for #{filename}: found, but is not a file"
				elsif !File.executable?(filename)
					log "Looking for #{filename}: found, but is not executable"
				else
					log "Looking for #{filename}: found"
					return filename
				end
			end
		end
		return nil
	end
	
	
	################ Directories ################
	
	# The absolute path to the Apache 2 'bin' directory, or nil if unknown.
	def self.apache2_bindir(options = {})
		apxs2 = options[:apxs2] || self.apxs2
		if apxs2.nil?
			return nil
		else
			return `#{apxs2} -q BINDIR 2>/dev/null`.strip
		end
	end
	memoize :apache2_bindir
	
	# The absolute path to the Apache 2 'sbin' directory, or nil if unknown.
	def self.apache2_sbindir(options = {})
		apxs2 = options[:apxs2] || self.apxs2
		if apxs2.nil?
			return nil
		else
			return `#{apxs2} -q SBINDIR`.strip
		end
	end
	memoize :apache2_sbindir
	
	
	################ Compiler and linker flags ################
	
	# The C compiler flags that are necessary to compile an Apache module.
	# Also includes APR and APU compiler flags if with_apr_flags is true.
	def self.apache2_module_cflags(with_apr_flags = true)
		flags = ["-fPIC"]
		if with_apr_flags
			flags << apr_flags
			flags << apu_flags
		end
		if !apxs2.nil?
			apxs2_flags = `#{apxs2} -q CFLAGS`.strip << " -I" << `#{apxs2} -q INCLUDEDIR`.strip
			apxs2_flags.gsub!(/-O\d? /, '')

			# Remove flags not supported by GCC
			if RUBY_PLATFORM =~ /solaris/ # TODO: Add support for people using SunStudio
				# The big problem is Coolstack apxs includes a bunch of solaris -x directives.
				options = apxs2_flags.split
				options.reject! { |f| f =~ /^\-x/ }
				options.reject! { |f| f =~ /^\-Xa/ }
				options.reject! { |f| f =~ /^\-fast/ }
				options.reject! { |f| f =~ /^\-mt/ }
				apxs2_flags = options.join(' ')
			end
			
			apxs2_flags.strip!
			flags << apxs2_flags
		end
		if !httpd.nil? && RUBY_PLATFORM =~ /darwin/
			# The default Apache install on OS X is a universal binary.
			# Figure out which architectures it's compiled for and do the same
			# thing for mod_passenger. We use the 'file' utility to do this.
			#
			# Running 'file' on the Apache executable usually outputs something
			# like this:
			#
			#   /usr/sbin/httpd: Mach-O universal binary with 4 architectures
			#   /usr/sbin/httpd (for architecture ppc7400):     Mach-O executable ppc
			#   /usr/sbin/httpd (for architecture ppc64):       Mach-O 64-bit executable ppc64
			#   /usr/sbin/httpd (for architecture i386):        Mach-O executable i386
			#   /usr/sbin/httpd (for architecture x86_64):      Mach-O 64-bit executable x86_64
			#
			# But on some machines, it may output just:
			#
			#   /usr/sbin/httpd: Mach-O fat file with 4 architectures
			#
			# (http://code.google.com/p/phusion-passenger/issues/detail?id=236)
			output = `file "#{httpd}"`.strip
			if output =~ /Mach-O fat file/ && output !~ /for architecture/
				architectures = ["i386", "ppc", "x86_64", "ppc64"]
			else
				architectures = []
				output.split("\n").grep(/for architecture/).each do |line|
					line =~ /for architecture (.*?)\)/
					architectures << $1
				end
			end
			# The compiler may not support all architectures in the binary.
			# XCode 4 seems to have removed support for the PPC architecture
			# even though there are still plenty of Apache binaries around
			# containing PPC components.
			architectures.reject! do |arch|
				!compiler_supports_architecture?(arch)
			end
			architectures.map! do |arch|
				"-arch #{arch}"
			end
			flags << architectures.compact.join(' ')
		end
		return flags.compact.join(' ').strip
	end
	memoize :apache2_module_cflags, true
	
	# Linker flags that are necessary for linking an Apache module.
	# Already includes APR and APU linker flags.
	def self.apache2_module_ldflags
		flags = "-fPIC #{apr_libs} #{apu_libs}"
		flags.strip!
		return flags
	end
	memoize :apache2_module_ldflags
	
	# The C compiler flags that are necessary for programs that use APR.
	def self.apr_flags
		return determine_apr_info[0]
	end
	
	# The linker flags that are necessary for linking programs that use APR.
	def self.apr_libs
		return determine_apr_info[1]
	end
	
	# The C compiler flags that are necessary for programs that use APR-Util.
	def self.apu_flags
		return determine_apu_info[0]
	end
	
	# The linker flags that are necessary for linking programs that use APR-Util.
	def self.apu_libs
		return determine_apu_info[1]
	end
	
	################ Miscellaneous information ################
	
	
	# Returns whether it is necessary to use information outputted by
	# 'apr-config' and 'apu-config' in order to compile an Apache module.
	# When Apache is installed with --with-included-apr, the APR/APU
	# headers are placed into the same directory as the Apache headers,
	# and so 'apr-config' and 'apu-config' won't be necessary in that case.
	def self.apr_config_needed_for_building_apache_modules?
		return !try_compile("whether APR is needed for building Apache modules",
			:c, "#include <apr.h>\n", apache2_module_cflags(false))
	end
	memoize :apr_config_needed_for_building_apache_modules?

private
	def self.determine_apr_info
		if apr_config.nil?
			return [nil, nil]
		else
			flags = `#{apr_config} --cppflags --includes`.strip
			libs = `#{apr_config} --link-ld`.strip
			flags.gsub!(/-O\d? /, '')
			if RUBY_PLATFORM =~ /solaris/
				# Remove flags not supported by GCC
				flags = flags.split(/ +/).reject{ |f| f =~ /^\-mt/ }.join(' ')
			elsif RUBY_PLATFORM =~ /aix/
				libs << " -Wl,-G -Wl,-brtl"
			end
			return [flags, libs]
		end
	end
	memoize :determine_apr_info, true
	private_class_method :determine_apr_info

	def self.determine_apu_info
		if apu_config.nil?
			return [nil, nil]
		else
			flags = `#{apu_config} --includes`.strip
			libs = `#{apu_config} --link-ld`.strip
			flags.gsub!(/-O\d? /, '')
			return [flags, libs]
		end
	end
	memoize :determine_apu_info
	private_class_method :determine_apu_info
end

end
