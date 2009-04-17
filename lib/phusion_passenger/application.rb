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

require 'rubygems'
require 'phusion_passenger/exceptions'
module PhusionPassenger

# Represents a single application instance.
class Application
	# The root directory of this application, i.e. the directory that contains
	# 'app/', 'public/', etc.
	attr_reader :app_root
	
	# The process ID of this application instance.
	attr_reader :pid
	
	# The name of the socket on which the application instance will accept
	# new connections. See #listen_socket_type on how one should interpret
	# this value.
	attr_reader :listen_socket_name
	
	# The type of socket that #listen_socket_name refers to. Currently this
	# is always 'unix', which means that #listen_socket_name refers to the
	# filename of a Unix domain socket.
	attr_reader :listen_socket_type
	
	# The owner pipe of the application instance (an IO object). Please see
	# RequestHandler for a description of the owner pipe.
	attr_reader :owner_pipe

	# - Returns the Ruby on Rails version that the application requires.
	# - Returns <tt>:vendor</tt> if the application has a vendored Rails.
	# - Returns nil if the application doesn't specify a particular version.
	# Raises VersionNotFound if the required Rails version is not installed.
	def self.detect_framework_version(app_root)
		if File.directory?("#{app_root}/vendor/rails/railties")
			# NOTE: We must check for 'rails/railties' and not just 'rails'.
			# Typo's vendor directory contains an empty 'rails' directory.
			return :vendor
		end
		
		environment_rb = File.read("#{app_root}/config/environment.rb")
		environment_rb =~ /^[^#]*RAILS_GEM_VERSION\s*=\s*["']([!~<>=]*\s*[\d.]+)["']/
		gem_version_spec = $1
		if gem_version_spec.nil?
			return nil
		end
		
		search_results = Gem.cache.search(Gem::Dependency.new('rails', gem_version_spec), true)
		found_version = search_results.map do |x|
			x.version.version
		end.sort.last
		if found_version.nil?
			# If this error was reported before, then the cache might be out of
			# date because the Rails version may have been installed now.
			# So we reload the RubyGems cache and try again.
			Gem.clear_paths
			search_results = Gem.cache.search(Gem::Dependency.new('rails', gem_version_spec), true)
			found_version = search_results.map do |x|
				x.version.version
			end.sort.last
		end
		
		if found_version.nil?
			raise VersionNotFound.new("There is no Ruby on Rails version " <<
				"installed that matches version \"#{gem_version_spec}\"",
				gem_version_spec)
		else
			return found_version
		end
	end

	# Creates a new instance of Application. The parameters correspond with the attributes
	# of the same names. No exceptions will be thrown.
	def initialize(app_root, pid, listen_socket_name, listen_socket_type, owner_pipe)
		@app_root = app_root
		@pid = pid
		@listen_socket_name = listen_socket_name
		@listen_socket_type = listen_socket_type
		@owner_pipe = owner_pipe
	end
	
	# Close the connection with the application instance. If there are no other
	# processes that have connections to this application instance, then it will
	# shutdown as soon as possible.
	#
	# See also AbstractRequestHandler#owner_pipe.
	def close
		@owner_pipe.close rescue nil
	end
end

end # module PhusionPassenger
