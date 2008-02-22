require 'rbconfig'

module PlatformInfo # :nodoc:
private
	def self.determine_multi_arch_flags
		if RUBY_PLATFORM =~ /darwin/
			return "-arch ppc7400 -arch ppc64 -arch i386 -arch x86_64"
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

	def self.env_defined?(name)
		return !ENV[name].nil? && !ENV[name]
	end

	def self.find_apache2ctl
		if env_defined?("APACHE2CTL")
			return ENV["APACHE2CTL"]
		elsif !`which apache2ctl`.empty?
			return "apache2ctl"
		elsif !`which apachectl`.empty?
			return "apachectl"
		else
			return nil
		end
	end
	
	def self.find_apxs2
		if env_defined?("APXS2")
			return ENV["APXS2"]
		elsif !`which apxs2`.empty?
			return "apxs2"
		elsif !`which apxs`.empty?
			return "apxs"
		else
			return nil
		end
	end
	
	def self.determine_apr1_info
		if `which pkg-config`.empty?
			if `which apr-1-config`.empty?
				return nil
			else
				flags = `apr-1-config --cppflags --includes`.strip
				libs = `apr-1-config --link-ld`.strip
			end
		else
			flags = `pkg-config --cflags apr-1 apr-util-1`.strip
			libs = `pkg-config --libs apr-1 apr-util-1`.strip
		end
		return [flags, libs]
	end

public
	RUBY = Config::CONFIG['bindir'] + '/' + Config::CONFIG['RUBY_INSTALL_NAME']
	MULTI_ARCH_FLAGS = determine_multi_arch_flags
	LIBEXT = determine_library_extension
	
	APACHE2CTL = find_apache2ctl
	APXS2 = find_apxs2
	if APXS2.nil?
		APXS2_FLAGS = nil
	else
		APXS2_FLAGS = `#{APXS2} -q CFLAGS`.strip << " -I" << `#{APXS2} -q INCLUDEDIR`.strip
	end
	
	APR1_FLAGS, APR1_LIBS = determine_apr1_info	
end
