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

require 'rubygems'
require 'phusion_passenger/exceptions'
module PhusionPassenger

# Contains various information about an application process.
class AppProcess
	# The root directory of this application process.
	attr_reader :app_root
	
	# This process's PID.
	attr_reader :pid
	
	# A hash containing all server sockets that this application process listens on.
	# The hash is in the form of:
	#
	#   {
	#      name1 => [socket_address1, socket_type1],
	#      name2 => [socket_address2, socket_type2],
	#      ...
	#   }
	#
	# +name+ is a Symbol. +socket_addressx+ is the address of the socket
	# and +socket_type1+ is the socket's type (either 'unix' or 'tcp').
	# There's guaranteed to be at least one server socket, namely one with the
	# name +:main+.
	attr_reader :server_sockets
	
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
	
	# Construct an AppProcess by reading information from the given MessageChannel.
	# The other side of the channel must be writing AppProcess information using
	# AppProcess#write_to_channel.
	#
	# Might raise SystemCallError, IOError or SocketError.
	def self.read_from_channel(channel)
		app_root, pid, n_server_sockets = channel.read
		if app_root.nil?
			raise IOError, "Connection closed"
		end
		
		server_sockets = {}
		n_server_sockets.to_i.times do
			message = channel.read
			if message.nil?
				raise IOError, "Connection closed"
			end
			name = message.shift
			server_sockets[name.to_sym] = message
		end
		
		owner_pipe = channel.recv_io
		
		return new(app_root, pid.to_i, owner_pipe, server_sockets)
	end
	
	# Write this AppProcess's information over the given MessageChannel.
	# The other side must read the information using AppProces.read_from_channel.
	#
	# Might raise SystemCallError, IOError or SocketError.
	def write_to_channel(channel)
		channel.write(@app_root, @pid, @server_sockets.size)
		@server_sockets.each_pair do |name, value|
			channel.write(name.to_s, *value)
		end
		channel.send_io(@owner_pipe)
	end
	
	# Creates a new AppProcess instance. The parameters correspond with the attributes
	# of the same names. No exceptions will be thrown.
	def initialize(app_root, pid, owner_pipe, server_sockets)
		@app_root   = app_root
		@pid        = pid
		@owner_pipe = owner_pipe
		
		# We copy the values like this so one can directly pass
		# AbstractRequestHandler#server_sockets as arguments
		# without having AppProcess store references to the socket
		# IO objects.
		@server_sockets = {}
		server_sockets.each_pair do |name, value|
			@server_sockets[name] = [value[0], value[1]]
		end
	end
	
	# Close the connection with the application process. If there are no other
	# processes that have connections to this application process, then it will
	# shutdown as soon as possible.
	#
	# See also AbstractRequestHandler#owner_pipe.
	def close
		@owner_pipe.close rescue nil
	end
end

end # module PhusionPassenger
