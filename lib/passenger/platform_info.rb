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
	def self.env_defined?(name)
		return !ENV[name].nil? && !ENV[name]
	end
	
	def self.determine_gem_command
		gem_exe_in_path = find_command("gem")
		correct_gem_exe = File.dirname(RUBY) + "/gem"
		if gem_exe_in_path.nil? || gem_exe_in_path == correct_gem_exe
			return "gem"
		else
			return correct_gem_exe
		end
	end

	def self.find_apxs2
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
	
	def self.determine_apache2_bindir
		if APXS2.nil?
			return nil
		else
			return `#{APXS2} -q BINDIR 2>/dev/null`.strip
		end
	end
	
	def self.determine_apache2_sbindir
		if APXS2.nil?
			return nil
		else
			return `#{APXS2} -q SBINDIR`.strip
		end
	end
	
	def self.find_apache2_executable(*possible_names)
		[APACHE2_BINDIR, APACHE2_SBINDIR].each do |bindir|
			if bindir.nil?
				next
			end
			possible_names.each do |name|
				filename = "#{bindir}/#{name}"
				if File.executable?(filename)
					return filename
				end
			end
		end
		return nil
	end
	
	def self.find_apache2ctl
		return find_apache2_executable('apache2ctl', 'apachectl')
	end
	
	def self.find_httpd
		if APXS2.nil?
			["apache2", "httpd2", "apache", "httpd"].each do |name|
				command = find_command(name)
				if !command.nil?
					return command
				end
			end
			return nil
		else
			return find_apache2_executable(`#{APXS2} -q TARGET`.strip)
		end
	end
	
	def self.determine_apxs2_flags
		if APXS2.nil?
			return nil
		else
			flags = `#{APXS2} -q CFLAGS`.strip << " -I" << `#{APXS2} -q INCLUDEDIR`
			flags.strip!
			flags.gsub!(/-O\d? /, '')
			return flags
		end
	end
	
	def self.determine_apr1_info
		if find_command('pkg-config')
			flags = `pkg-config --cflags apr-1 apr-util-1 2>/dev/null`.strip
			libs = `pkg-config --libs apr-1 apr-util-1 2>/dev/null`.strip
		end
		if flags.empty? && libs.empty?
			apr_config = find_command('apr-1-config')
			if apr_config.nil?
				apr_config = find_command('apr-config')
			end
			if apr_config.nil?
				return nil
			else
				flags = `#{apr_config} --cppflags --includes`.strip
				libs = `#{apr_config} --link-ld`.strip
			end
		end
		flags.gsub!(/-O\d? /, '')
		return [flags, libs]
	end
	
	def self.determine_multi_arch_flags
		if RUBY_PLATFORM =~ /darwin/ && !HTTPD.nil?
			architectures = []
			`file "#{HTTPD}"`.split("\n").grep(/for architecture/).each do |line|
				line =~ /for architecture (.*?)\)/
				architectures << "-arch #{$1}"
			end
			return architectures.join(' ')
		else
			return ""
		end
	end
	
	def self.determine_library_extension
		if RUBY_PLATFORM =~ /darwin/
			return "bundle"
		else
			return "so"
		end
	end
	
	def self.read_file(filename)
		return File.read(filename)
	rescue
		return ""
	end
	
	def self.determine_linux_distro
		if RUBY_PLATFORM !~ /linux/
			return nil
		end
		lsb_release = read_file("/etc/lsb-release")
		if lsb_release =~ /Ubuntu/
			return :ubuntu
		elsif File.exist?("/etc/debian_version")
			return :debian
		elsif File.exist?("/etc/redhat-release")
			redhat_release = read_file("/etc/redhat-release")
			if redhat_release =~ /CentOS/
				return :centos
			elsif redhat_release =~ /Fedora/  # is this correct?
				return :fedora
			else
				return :rhel
			end
		elsif File.exist?("/etc/suse-release")
			return :suse
		elsif File.exist?("/etc/gentoo-release")
			return :gentoo
		else
			return :unknown
		end
		# TODO: Slackware, Mandrake/Mandriva
	end

public
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

	# The absolute path to the current Ruby interpreter.
	RUBY = Config::CONFIG['bindir'] + '/' + Config::CONFIG['RUBY_INSTALL_NAME']
	# The correct 'gem' command for this Ruby interpreter.
	GEM = determine_gem_command
	
	# The absolute path to the 'apxs' or 'apxs2' executable.
	APXS2 = find_apxs2
	# The absolute path to the Apache 2 'bin' directory.
	APACHE2_BINDIR = determine_apache2_bindir
	# The absolute path to the Apache 2 'sbin' directory.
	APACHE2_SBINDIR = determine_apache2_sbindir
	# The absolute path to the 'apachectl' or 'apache2ctl' binary.
	APACHE2CTL = find_apache2ctl
	# The absolute path to the Apache binary (that is, 'httpd', 'httpd2', 'apache' or 'apache2').
	HTTPD = find_httpd
	
	# The C compiler flags that are necessary to compile an Apache module.
	APXS2_FLAGS = determine_apxs2_flags
	# The C compiler flags that are necessary for programs that use APR.
	APR1_FLAGS, APR1_LIBS = determine_apr1_info
	
	# The C compiler flags that are necessary for building binaries in the same architecture(s) as Apache.
	MULTI_ARCH_FLAGS = determine_multi_arch_flags
	# The current platform's shared library extension ('so' on most Unices).
	LIBEXT = determine_library_extension
	# An identifier for the current Linux distribution. nil if the operating system is not Linux.
	LINUX_DISTRO = determine_linux_distro
end
