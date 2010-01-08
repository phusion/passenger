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

require 'phusion_passenger/platform_info'
module PhusionPassenger

# Represents a dependency software that Passenger requires. It's used by the
# installer to check whether all dependencies are available. A Dependency object
# contains full information about a dependency, such as its name, code for
# detecting whether it is installed, and installation instructions for the
# current platform.
class Dependency # :nodoc: all
	[:name, :install_command, :install_instructions, :install_comments,
	 :website, :website_comments, :provides].each do |attr_name|
		attr_writer attr_name
		
		define_method(attr_name) do
			call_init_block
			return instance_variable_get("@#{attr_name}")
		end
	end
	
	def initialize(&block)
		@included_by = []
		@init_block = block
	end
	
	def define_checker(&block)
		@checker = block
	end
	
	def check
		call_init_block
		result = Result.new
		@checker.call(result)
		return result
	end

private
	class Result
		def found(*args)
			if args.empty?
				@found = true
			else
				@found = args.first
			end
		end
		
		def not_found
			found(false)
		end
		
		def found?
			return !@found.nil? && @found
		end
		
		def found_at
			if @found.is_a?(TrueClass) || @found.is_a?(FalseClass)
				return nil
			else
				return @found
			end
		end
	end

	def call_init_block
		if @init_block
			init_block = @init_block
			@init_block = nil
			init_block.call(self)
		end
	end
end

# Namespace which contains the different dependencies that Passenger may require.
# See Dependency for more information.
module Dependencies # :nodoc: all
	# Returns whether fastthread is a required dependency for the current
	# Ruby interpreter.
	def self.fastthread_required?
		return (!defined?(RUBY_ENGINE) || RUBY_ENGINE == "ruby") && RUBY_VERSION < "1.8.7"
	end

	GCC = Dependency.new do |dep|
		dep.name = "GNU C++ compiler"
		dep.define_checker do |result|
			gxx = PlatformInfo.find_command('g++')
			if gxx.nil?
				result.not_found
			else
				result.found(gxx)
			end
		end
		if RUBY_PLATFORM =~ /linux/
			tags = PlatformInfo.linux_distro_tags
			if tags.include?(:debian)
				dep.install_command = "apt-get install build-essential"
			elsif tags.include?(:mandriva)
				dep.install_command = "urpmi gcc-c++"
			elsif tags.include?(:redhat)
				dep.install_command = "yum install gcc-c++"
			elsif tags.include?(:gentoo)
				dep.install_command = "emerge -av gcc"
			end
		elsif RUBY_PLATFORM =~ /darwin/
			dep.install_instructions = "Please install the Apple Development Tools: http://developer.apple.com/tools/"
		end
		dep.website = "http://gcc.gnu.org/"
	end
	
	Ruby_DevHeaders = Dependency.new do |dep|
		dep.name = "Ruby development headers"
		dep.define_checker do |result|
			require 'rbconfig'
			begin
				require 'mkmf'
				header_dir = Config::CONFIG['rubyhdrdir'] || Config::CONFIG['archdir']
				result.found(File.exist?("#{header_dir}/ruby.h"))
			rescue LoadError, SystemExit
				# On RedHat/Fedora/CentOS, if ruby-devel is not installed then
				# mkmf.rb will print an error and call 'exit'. So here we
				# catch SystemExit.
				result.not_found
			end
		end
		if RUBY_PLATFORM =~ /linux/
			tags = PlatformInfo.linux_distro_tags
			if tags.include?(:debian)
				dep.install_command = "apt-get install ruby1.8-dev"
			elsif tags.include?(:mandriva)
				dep.install_command = "urpmi urpmi ruby-RubyGems"
			elsif tags.include?(:redhat)
				dep.install_command = "yum install ruby-devel"
			elsif tags.include?(:gentoo)
				dep.install_command = "emerge -av ruby"
			end
		elsif RUBY_PLATFORM =~ /freebsd/
			dep.install_command = "make -C /usr/ports/lang/ruby18 install"
		end
		dep.website = "http://www.ruby-lang.org/"
		dep.install_instructions = "Please reinstall Ruby by downloading it from <b>#{dep.website}</b>"
	end
	
	Ruby_OpenSSL = Dependency.new do |dep|
		dep.name = "OpenSSL support for Ruby"
		dep.define_checker do |result|
			begin
				require 'openssl'
				result.found
			rescue LoadError
				result.not_found
			end
		end
		if RUBY_PLATFORM =~ /linux/
			case PlatformInfo.linux_distro
			when :ubuntu, :debian
				dep.install_command = "apt-get install libopenssl-ruby"
			end
		end
		if dep.install_command.nil?
			dep.website = "http://www.ruby-lang.org/"
			dep.install_instructions = "Please (re)install Ruby with OpenSSL " <<
				"support by downloading it from <b>#{dep.website}</b>."
		end
	end
	
	RubyGems = Dependency.new do |dep|
		dep.name = "RubyGems"
		dep.define_checker do |result|
			begin
				require 'rubygems'
				result.found
			rescue LoadError
				result.not_found
			end
		end
		dep.website = "http://www.rubygems.org/"
		dep.install_instructions = "Please download it from <b>#{dep.website}</b>. " <<
			"Extract the tarball, and run <b>ruby setup.rb</b>"
	end
	
	Rake = Dependency.new do |dep|
		dep.name = "Rake"
		dep.define_checker do |result|
			if PlatformInfo.rake.nil?
				result.not_found
			else
				result.found(PlatformInfo.rake)
			end
		end
		dep.website = "http://rake.rubyforge.org/"
		dep.install_instructions = "Please install RubyGems first, then run <b>#{PlatformInfo::GEM || "gem"} install rake</b>"
	end
	
	Apache2 = Dependency.new do |dep|
		dep.name = "Apache 2"
		dep.define_checker do |result|
			if PlatformInfo.httpd.nil?
				result.not_found
			else
				result.found(PlatformInfo.httpd)
			end
		end
		if RUBY_PLATFORM =~ /linux/
			tags = PlatformInfo.linux_distro_tags
			if tags.include?(:debian)
				dep.install_command = "apt-get install apache2-mpm-prefork"
			elsif tags.include?(:mandriva)
				dep.install_command = "urpmi apache"
			elsif tags.include?(:redhat)
				dep.install_command = "yum install httpd"
			elsif tags.include?(:gentoo)
				dep.install_command = "emerge -av apache"
			end
		elsif RUBY_PLATFORM =~ /freebsd/
			dep.install_command = "make -C /usr/ports/www/apache22 install"
			dep.provides = [Apache2_DevHeaders, APR_DevHeaders, APU_DevHeaders]
		end
		dep.website = "http://httpd.apache.org/"
	end
	
	Apache2_DevHeaders = Dependency.new do |dep|
		dep.name = "Apache 2 development headers"
		dep.define_checker do |result|
			if PlatformInfo.apxs2.nil?
				result.not_found
			else
				result.found(PlatformInfo.apxs2)
			end
		end
		if RUBY_PLATFORM =~ /linux/
			tags = PlatformInfo.linux_distro_tags
			if tags.include?(:debian)
				dep.install_command = "apt-get install apache2-prefork-dev"
				dep.provides = [Apache2]
			elsif tags.include?(:mandriva)
				dep.install_command = "urpmi apache-devel"
				dep.provides = [Apache2]
			elsif tags.include?(:redhat)
				dep.install_command = "yum install httpd-devel"
				dep.provides = [Apache2]
			elsif tags.include?(:gentoo)
				dep.install_command = "emerge -av apache"
				dep.provides = [Apache2]
			end
		elsif RUBY_PLATFORM =~ /freebsd/
			dep.install_command = "make -C /usr/ports/www/apache22 install"
		end
		dep.website = "http://httpd.apache.org/"
	end
	
	APR_DevHeaders = Dependency.new do |dep|
		dep.name = "Apache Portable Runtime (APR) development headers"
		dep.define_checker do |result|
			if PlatformInfo.apr_config.nil?
				result.not_found
			else
				result.found(PlatformInfo.apr_config)
			end
		end
		if RUBY_PLATFORM =~ /linux/
			tags = PlatformInfo.linux_distro_tags
			if tags.include?(:debian)
				dep.install_command = "apt-get install libapr1-dev"
			elsif tags.include?(:mandriva)
				dep.install_command = "urpmi libapr-devel"
			elsif tags.include?(:redhat)
				dep.install_command = "yum install apr-devel"
			elsif tags.include?(:gentoo)
				dep.install_command = "emerge -av apr"
			end
		elsif RUBY_PLATFORM =~ /darwin/
			dep.install_instructions = "Please install Apache from MacPorts, which will " <<
				"provide APR automatically. <b>Or</b>, if you're installing against MacOS X's " <<
				"default provided Apache, then please install the OS X Developer SDK."
		end
		dep.website = "http://httpd.apache.org/"
		dep.website_comments = "APR is an integrated part of Apache."
	end

	APU_DevHeaders = Dependency.new do |dep|
		dep.name = "Apache Portable Runtime Utility (APU) development headers"
		dep.define_checker do |result|
			if PlatformInfo.apu_config.nil?
				result.not_found
			else
				result.found(PlatformInfo.apu_config)
			end
		end
		if RUBY_PLATFORM =~ /linux/
			tags = PlatformInfo.linux_distro_tags
			if tags.include?(:debian)
				dep.install_command = "apt-get install libaprutil1-dev"
			elsif tags.include?(:mandriva)
				dep.install_command = "urpmi libapr-util-devel"
			end
		elsif RUBY_PLATFORM =~ /darwin/
			dep.install_instructions = "Please install Apache from MacPorts, which will " <<
				"provide APU automatically. <b>Or</b>, if you're installing against MacOS X's " <<
				"default provided Apache, then please install the OS X Developer SDK."
		end
		dep.website = "http://httpd.apache.org/"
		dep.website_comments = "APR Utility is an integrated part of Apache."
	end
	
	FastThread = Dependency.new do |dep|
		dep.name = "fastthread"
		dep.define_checker do |result|
			begin
				begin
					require 'rubygems'
				rescue LoadError
				end
				require 'fastthread'
				result.found
			rescue LoadError
				result.not_found
			end
		end
		dep.install_instructions = "Please install RubyGems first, then run <b>#{PlatformInfo::GEM || "gem"} install fastthread</b>"
	end

	Rack = Dependency.new do |dep|
		dep.name = "rack"
		dep.define_checker do |result|
			begin
				begin
					require 'rubygems'
				rescue LoadError
				end
				require 'rack'
				result.found
			rescue LoadError
				result.not_found
			end
		end
		dep.install_instructions = "Please install RubyGems first, then run <b>#{PlatformInfo::GEM || "gem"} install rack</b>"
	end
	
	OpenSSL_Dev = Dependency.new do |dep|
		dep.name = "OpenSSL development headers"
		dep.define_checker do |result|
			source_file = '/tmp/passenger-openssl-check.c'
			object_file = '/tmp/passenger-openssl-check.o'
			begin
				File.open(source_file, 'w') do |f|
					f.write("#include <openssl/ssl.h>")
				end
				Dir.chdir(File.dirname(source_file)) do
					if system("(gcc #{ENV['CFLAGS']} -c '#{source_file}') >/dev/null 2>/dev/null")
						result.found
					else
						result.not_found
					end
				end
			ensure
				File.unlink(source_file) rescue nil
				File.unlink(object_file) rescue nil
			end
		end
		if RUBY_PLATFORM =~ /linux/
			tags = PlatformInfo.linux_distro_tags
			if tags.include?(:debian)
				dep.install_command = "apt-get install libssl-dev"
			elsif tags.include?(:redhat)
				dep.install_command = "yum install openssl-devel"
			end
		end
		dep.website = "http://www.openssl.org/"
	end
	
	Zlib_Dev = Dependency.new do |dep|
		dep.name = "Zlib development headers"
		dep.define_checker do |result|
			begin
				File.open('/tmp/r8ee-check.c', 'w') do |f|
					f.write("#include <zlib.h>")
				end
				Dir.chdir('/tmp') do
					if system("(g++ -c r8ee-check.c) >/dev/null 2>/dev/null")
						result.found
					else
						result.not_found
					end
				end
			ensure
				File.unlink('/tmp/r8ee-check.c') rescue nil
				File.unlink('/tmp/r8ee-check.o') rescue nil
			end
		end
		if RUBY_PLATFORM =~ /linux/
			tags = PlatformInfo.linux_distro_tags
			if tags.include?(:debian)
				dep.install_command = "apt-get install zlib1g-dev"
			elsif tags.include?(:mandriva)
				dep.install_command = "urpmi zlib1-devel"
			elsif tags.include?(:redhat)
				dep.install_command = "yum install zlib-devel"
			end
		end
		dep.website = "http://www.zlib.net/"
	end
end

end # module PhusionPassenger
