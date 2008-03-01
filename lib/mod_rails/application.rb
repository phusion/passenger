require 'rubygems'
module Passenger

# Indicates that there is no Ruby on Rails version installed that satisfies
# a given Ruby on Rails Gem version specification.
class VersionNotFound < StandardError
	attr_reader :gem_version_spec
	
	# - +message+: The exception message.
	# - +gem_version_spec+: The Ruby on Rails Gem version specification that caused this error.
	def initialize(message, gem_version_spec)
		super(message)
		@gem_version_spec = gem_version_spec
	end
end

# TODO: synchronize this documentation with the C++ one
# Represents a single running instance of a Ruby on Rails application.
class Application
	# The root directory of this application, i.e. the directory that contains 'app/', 'public/', etc.
	attr_reader :app_root
	
	# The process ID of this application instance.
	attr_reader :pid
	
	# The name of the Unix socket on which the application instance will accept
	# new connections.
	attr_reader :listen_socket_name
	
	attr_reader :owner_pipe

	# Return the Ruby on Rails version that the application requires.
	# Returns nil if the application has a vendored Rails.
	# Raises VersionNotFound if the required Rails version is not installed.
	def self.detect_framework_version(app_root)
		if File.directory?("#{app_root}/vendor/rails")
			return nil
		end
		
		environment_rb = File.read("#{app_root}/config/environment.rb")
		environment_rb =~ /^[^#]*RAILS_GEM_VERSION\s*=\s*["']([!~<>=]*\s*[\d.]+)["']/
		gem_version_spec = $1
		found_version = Gem.cache.search('rails', gem_version_spec).map do |x|
			x.version.version
		end.sort.last
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
	# Note that abstract namespace Unix sockets are only supported on Linux
	# at the moment.
	def using_abstract_namespace?
		return @using_abstract_namespace
	end
	
	# Close the connection with the application instance. If there are no other
	# processes that have connections to this application instance, then it will
	# shutdown as soon as possible.
	def close
		@owner_pipe.close
	end
end

end # module Passenger
