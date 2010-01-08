#  Phusion Passenger - http://www.modrails.com/
#  Copyright (c) 2008, 2009 Phusion
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

# Wow, I can't believe in how many ways one can build Apache in OS
# X! We have to resort to all sorts of tricks to make Passenger build
# out of the box on OS X. :-(
#
# In the name of usability and the "end user is the king" line of thought,
# I shall suffer the horrible faith of writing tons of autodetection code!

# This module autodetects various platform-specific information, and
# provides that information through constants.
#
# Users can change the detection behavior by setting the environment variable
# <tt>APXS2</tt> to the correct 'apxs' (or 'apxs2') binary, as provided by
# Apache.
module PlatformInfo
private
	# Turn the specified class method into a memoized one. If the given
	# class method is called without arguments, then its result will be
	# memoized, frozen, and returned upon subsequent calls without arguments.
	# Calls with arguments are never memoized.
	#
	#   def self.foo(max = 10)
	#      return rand(max)
	#   end
	#   memoize :foo
	#   
	#   foo   # => 3
	#   foo   # => 3
	#   foo(100)   # => 49
	#   foo(100)   # => 26
	#   foo   # => 3
	def self.memoize(method)
		metaclass = class << self; self; end
		metaclass.send(:alias_method, "_unmemoized_#{method}", method)
		variable_name = "@@memoized_#{method}".sub(/\?/, '')
		check_variable_name = "@@has_memoized_#{method}".sub(/\?/, '')
		eval("#{variable_name} = nil")
		eval("#{check_variable_name} = false")
		source = %Q{
		   def self.#{method}(*args)                                # def self.httpd(*args)
		      if args.empty?                                        #    if args.empty?
		         if !#{check_variable_name}                         #       if !@@has_memoized_httpd
		            #{variable_name} = _unmemoized_#{method}.freeze #          @@memoized_httpd = _unmemoized_httpd.freeze
		            #{check_variable_name} = true                   #          @@has_memoized_httpd = true
		         end                                                #       end
		         return #{variable_name}                            #       return @@memoized_httpd
		      else                                                  #    else
		         return _unmemoized_#{method}(*args)                #       return _unmemoized_httpd(*args)
		      end                                                   #    end
		   end                                                      # end
		}
		class_eval(source)
	end
	
	def self.env_defined?(name)
		return !ENV[name].nil? && !ENV[name].empty?
	end
	
	def self.locate_ruby_executable(name)
		if RUBY_PLATFORM =~ /darwin/ &&
		   RUBY =~ %r(\A/System/Library/Frameworks/Ruby.framework/Versions/.*?/usr/bin/ruby\Z)
			# On OS X we must look for Ruby binaries in /usr/bin.
			# RubyGems puts executables (e.g. 'rake') in there, not in
			# /System/Libraries/(...)/bin.
			filename = "/usr/bin/#{name}"
		else
			filename = File.dirname(RUBY) + "/#{name}"
		end
		if File.file?(filename) && File.executable?(filename)
			return filename
		else
			# RubyGems might put binaries in a directory other
			# than Ruby's bindir. Debian packaged RubyGems and
			# DebGem packaged RubyGems are the prime examples.
			begin
				require 'rubygems' unless defined?(Gem)
				filename = Gem.bindir + "/#{name}"
				if File.file?(filename) && File.executable?(filename)
					return filename
				else
					return nil
				end
			rescue LoadError
				return nil
			end
		end
	end
	
	# Look in the directory +dir+ and check whether there's an executable
	# whose base name is equal to one of the elements in +possible_names+.
	# If so, returns the full filename. If not, returns nil.
	def self.select_executable(dir, *possible_names)
		possible_names.each do |name|
			filename = "#{dir}/#{name}"
			if File.file?(filename) && File.executable?(filename)
				return filename
			end
		end
		return nil
	end

	def self.find_apache2_executable(*possible_names)
		[apache2_bindir, apache2_sbindir].each do |bindir|
			if bindir.nil?
				next
			end
			possible_names.each do |name|
				filename = "#{bindir}/#{name}"
				if File.file?(filename) && File.executable?(filename)
					return filename
				end
			end
		end
		return nil
	end
	
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
	memoize :determine_apr_info

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
	
	def self.read_file(filename)
		return File.read(filename)
	rescue
		return ""
	end

public
	# The absolute path to the current Ruby interpreter.
	RUBY = Config::CONFIG['bindir'] + '/' + Config::CONFIG['RUBY_INSTALL_NAME'] + Config::CONFIG['EXEEXT']
	# The correct 'gem' command for this Ruby interpreter.
	GEM = locate_ruby_executable('gem')
	
	# Check whether the specified command is in $PATH, and return its
	# absolute filename. Returns nil if the command is not found.
	#
	# This function exists because system('which') doesn't always behave
	# correctly, for some weird reason.
	def self.find_command(name)
		ENV['PATH'].split(File::PATH_SEPARATOR).detect do |directory|
			path = File.join(directory, name.to_s)
			if File.executable?(path)
				return path
			end
		end
		return nil
	end
	
	
	################ Programs ################
	
	
	# Returns the absolute path to the Rake executable that
	# belongs to the current Ruby interpreter. Returns nil if it
	# doesn't exist.
	def self.rake
		return locate_ruby_executable('rake')
	end
	memoize :rake
	
	# Returns the absolute path to the RSpec runner program that
	# belongs to the current Ruby interpreter. Returns nil if it
	# doesn't exist.
	def self.rspec
		return locate_ruby_executable('spec')
	end
	memoize :rspec
	
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
	
	# The absolute path to the 'apachectl' or 'apache2ctl' binary.
	def self.apache2ctl
		return find_apache2_executable('apache2ctl', 'apachectl2', 'apachectl')
	end
	memoize :apache2ctl
	
	# The absolute path to the Apache binary (that is, 'httpd', 'httpd2', 'apache' or 'apache2').
	def self.httpd
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
			return find_apache2_executable(`#{apxs2} -q TARGET`.strip)
		end
	end
	memoize :httpd
	
	# The absolute path to the 'apr-config' or 'apr-1-config' executable.
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
	
	# The absolute path to the 'apu-config' or 'apu-1-config' executable.
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
	
	
	################ Directories ################
	
	
	# The absolute path to the Apache 2 'bin' directory.
	def self.apache2_bindir
		if apxs2.nil?
			return nil
		else
			return `#{apxs2} -q BINDIR 2>/dev/null`.strip
		end
	end
	memoize :apache2_bindir
	
	# The absolute path to the Apache 2 'sbin' directory.
	def self.apache2_sbindir
		if apxs2.nil?
			return nil
		else
			return `#{apxs2} -q SBINDIR`.strip
		end
	end
	memoize :apache2_sbindir
	
	
	################ Compiler and linker flags ################
	
	
	# Compiler flags that should be used for compiling every C/C++ program,
	# for portability reasons. These flags should be specified as last
	# when invoking the compiler.
	def self.portability_cflags
		flags = ["-D_REENTRANT -I/usr/local/include"]
		if RUBY_PLATFORM =~ /solaris/
			flags << '-D_XOPEN_SOURCE=500 -D_XPG4_2 -D__EXTENSIONS__ -D__SOLARIS__ -D_FILE_OFFSET_BITS=64'
			flags << '-DBOOST_HAS_STDINT_H' unless RUBY_PLATFORM =~ /solaris2.9/
			flags << '-D__SOLARIS9__ -DBOOST__STDC_CONSTANT_MACROS_DEFINED' if RUBY_PLATFORM =~ /solaris2.9/
			flags << '-mcpu=ultrasparc' if RUBY_PLATFORM =~ /sparc/
		elsif RUBY_PLATFORM =~ /openbsd/
			flags << '-DBOOST_HAS_STDINT_H -D_GLIBCPP__PTHREADS'
		elsif RUBY_PLATFORM =~ /aix/
			flags << '-DOXT_DISABLE_BACKTRACES'
		elsif RUBY_PLATFORM =~ /(sparc-linux|arm-linux|sh4-linux)/
			# http://code.google.com/p/phusion-passenger/issues/detail?id=200
			# http://groups.google.com/group/phusion-passenger/t/6b904a962ee28e5c
			flags << '-DBOOST_SP_USE_PTHREADS'
		end
		return flags.compact.join(" ").strip
	end
	memoize :portability_cflags
	
	# Linker flags that should be used for linking every C/C++ program,
	# for portability reasons. These flags should be specified as last
	# when invoking the linker.
	def self.portability_ldflags
		if RUBY_PLATFORM =~ /solaris/
			return '-lxnet -lrt -lsocket -lnsl -lpthread'
		else
			return '-lpthread'
		end
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
	
	# The C compiler flags that are necessary to compile an Apache module.
	# Possibly includes APR and APU compiler flags.
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
				architectures = ["-arch i386 -arch ppc -arch x86_64 -arch ppc64"]
			else
				architectures = []
				output.split("\n").grep(/for architecture/).each do |line|
					line =~ /for architecture (.*?)\)/
					architectures << "-arch #{$1}"
				end
			end
			flags << architectures.compact.join(' ')
		end
		return flags.compact.join(' ').strip
	end
	memoize :apache2_module_cflags
	
	# Linker flags that are necessary for linking an Apache module.
	# Possibly includes APR and APU linker flags.
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
		filename = File.join("/tmp/passenger-platform-check-#{Process.pid}.c")
		File.open(filename, "w") do |f|
			f.puts("#include <apr.h>")
		end
		begin
			return !system("(gcc #{apache2_module_cflags(false)} -c '#{filename}' -o '#{filename}.o') >/dev/null 2>/dev/null")
		ensure
			File.unlink(filename) rescue nil
			File.unlink("#{filename}.o") rescue nil
		end
	end
	memoize :apr_config_needed_for_building_apache_modules?

	# The current platform's shared library extension ('so' on most Unices).
	def self.library_extension
		if RUBY_PLATFORM =~ /darwin/
			return "bundle"
		else
			return "so"
		end
	end
	
	# An identifier for the current Linux distribution. nil if the operating system is not Linux.
	def self.linux_distro
		tags = linux_distro_tags
		if tags
			return tags.first
		else
			return nil
		end
	end
	
	# Autodetects the current Linux distribution and return a number of identifier tags.
	# The first tag identifies the distribution while the other tags indicate which
	# distributions it is likely compatible with.
	# Returns nil if the operating system is not Linux.
	def self.linux_distro_tags
		if RUBY_PLATFORM !~ /linux/
			return nil
		end
		lsb_release = read_file("/etc/lsb-release")
		if lsb_release =~ /Ubuntu/
			return [:ubuntu, :debian]
		elsif File.exist?("/etc/debian_version")
			return [:debian]
		elsif File.exist?("/etc/redhat-release")
			redhat_release = read_file("/etc/redhat-release")
			if redhat_release =~ /CentOS/
				return [:centos, :redhat]
			elsif redhat_release =~ /Fedora/
				return [:fedora, :redhat]
			elsif redhat_release =~ /Mandriva/
				return [:mandriva, :redhat]
			else
				# On official RHEL distros, the content is in the form of
				# "Red Hat Enterprise Linux Server release 5.1 (Tikanga)"
				return [:rhel, :redhat]
			end
		elsif File.exist?("/etc/suse-release")
			return [:suse]
		elsif File.exist?("/etc/gentoo-release")
			return [:gentoo]
		else
			return [:unknown]
		end
		# TODO: Slackware
	end
	memoize :linux_distro_tags
end
