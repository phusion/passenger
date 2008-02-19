module ModRails # :nodoc:

# TODO: synchronize this documentation with the C++ one
# Represents a single running instance of a Ruby on Rails application.
class Application
	# The root directory of this application, i.e. the directory that contains 'app/', 'public/', etc.
	attr_reader :app_root
	
	# The process ID of this application instance.
	attr_reader :pid
	
	attr_reader :listen_socket

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
	def initialize(app_root, pid, listen_socket)
		@app_root = app_root
		@pid = pid
		@listen_socket = listen_socket
	end
	
	def close
		@listen_socket.close
	end
end

end # module ModRails
