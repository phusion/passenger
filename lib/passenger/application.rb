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

require 'rubygems'
require 'passenger/exceptions'
module Passenger

# Represents a single application instance.
class Application
	# The root directory of this application, i.e. the directory that contains
	# 'app/', 'public/', etc.
	attr_reader :app_root
	
	# The process ID of this application instance.
	attr_reader :pid
	
	# The name of the Unix socket on which the application instance will accept
	# new connections.
	attr_reader :listen_socket_name
	
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
		
		found_version = Gem.cache.search('rails', gem_version_spec).map do |x|
			x.version.version
		end.sort.last
		if found_version.nil?
			# If this error was reported before, then the cache might be out of
			# date because the Rails version may have been installed now.
			# So we reload the RubyGems cache and try again.
			Gem.clear_paths
			found_version = Gem.cache.search('rails', gem_version_spec).map do |x|
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
	def initialize(app_root, pid, listen_socket_name, using_abstract_namespace, owner_pipe)
		@app_root = app_root
		@pid = pid
		@listen_socket_name = listen_socket_name
		@using_abstract_namespace = using_abstract_namespace
		@owner_pipe = owner_pipe
	end
	
	# Whether _listen_socket_name_ refers to a Unix socket in the abstract namespace.
	# In any case, _listen_socket_name_ does *not* contain the leading null byte.
	#
	# Note that at the moment, only Linux seems to support abstract namespace Unix
	# sockets.
	def using_abstract_namespace?
		return @using_abstract_namespace
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

end # module Passenger
