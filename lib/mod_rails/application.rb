module ModRails # :nodoc:

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

	# Return the Ruby on Rails version that the application requires, or nil
	# if it doesn't require a particular version.
	#
	# The version string is a RubyGems version specification. So it might
	# also look like ">= 1.2.2" or "~> 2.0.0".
	def self.get_framework_version(app_root)
		File.read("#{app_root}/config/environment.rb") =~ /^[^#]*RAILS_GEM_VERSION\s+=\s+'([\d.]+)'/
		return $1
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

end # module ModRails
