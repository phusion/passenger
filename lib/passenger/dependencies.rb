#  Phusion Passenger - http://www.modrails.com/
#  Copyright (C) 2008  Phusion
#
#  Phusion Passenger is a trademark of Hongli Lai & Ninh Bui.
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

require 'passenger/platform_info'
module Passenger

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
		def found(filename_or_boolean = nil)
			if filename_or_boolean.nil?
				@found = true
			else
				@found = filename_or_boolean
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
	include PlatformInfo
	
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
			case LINUX_DISTRO
			when :ubuntu, :debian
				dep.install_command = "apt-get install build-essential"
			when :rhel, :fedora, :centos
				dep.install_command = "yum install gcc-c++"
			when :gentoo
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
				result.found(File.exist?(Config::CONFIG['archdir'] + "/ruby.h"))
			rescue LoadError
				result.not_found
			end
		end
		if RUBY_PLATFORM =~ /linux/
			case LINUX_DISTRO
			when :ubuntu, :debian
				dep.install_command = "apt-get install ruby1.8-dev"
			when :rhel, :fedora, :centos
				dep.install_command = "yum install ruby-devel"
			when :gentoo
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
			case LINUX_DISTRO
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
			rake = PlatformInfo.find_command("rake")
			if rake.nil?
				result.not_found
			else
				result.found(rake)
			end
		end
		dep.website = "http://rake.rubyforge.org/"
		dep.install_instructions = "Please install RubyGems first, then run <b>gem install rake</b>"
	end
	
	Apache2 = Dependency.new do |dep|
		dep.name = "Apache 2"
		dep.define_checker do |result|
			if HTTPD.nil?
				result.not_found
			else
				result.found(HTTPD)
			end
		end
		if RUBY_PLATFORM =~ /linux/
			case LINUX_DISTRO
			when :ubuntu, :debian
				dep.install_command = "apt-get install apache2-mpm-prefork"
			when :rhel, :fedora, :centos
				dep.install_command = "yum install httpd"
			when :gentoo
				dep.install_command = "emerge -av apache"
			end
		elsif RUBY_PLATFORM =~ /freebsd/
			dep.install_command = "make -C /usr/ports/www/apache22 install"
			dep.provides = [Apache2_DevHeaders, APR_DevHeaders]
		end
		dep.website = "http://httpd.apache.org/"
	end
	
	Apache2_DevHeaders = Dependency.new do |dep|
		dep.name = "Apache 2 development headers"
		dep.define_checker do |result|
			if APXS2.nil?
				result.not_found
			else
				result.found(APXS2)
			end
		end
		if RUBY_PLATFORM =~ /linux/
			case LINUX_DISTRO
			when :ubuntu, :debian
				dep.install_command = "apt-get install apache2-prefork-dev"
				dep.provides = [Apache2]
			when :rhel, :fedora, :centos
				dep.install_command = "yum install httpd-devel"
				dep.provides = [Apache2]
			when :gentoo
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
			result.found(APR_CONFIG)
		end
		if RUBY_PLATFORM =~ /linux/
			case LINUX_DISTRO
			when :ubuntu, :debian
				dep.install_command = "apt-get install libapr1-dev"
			when :rhel, :fedora, :centos
				dep.install_command = "yum install apr-devel"
			when :gentoo
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
		dep.install_instructions = "Please install RubyGems first, then run <b>gem install fastthread</b>"
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
		dep.install_instructions = "Please install RubyGems first, then run <b>gem install rack</b>"
	end
end

end # module Passenger
