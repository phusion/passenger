module ModRails # :nodoc:

# Represents a single running instance of a Ruby on Rails application.
class Application
	# The root directory of this application, i.e. the directory that contains 'app/', 'public/', etc.
	attr_reader :app_root
	
	# The process ID of this application instance.
	attr_reader :pid
	
	# An IO object, used for reading data from the application instance.
	# This, together with _writer_, is the application's main communication
	# channel with the outside world.
	#
	# This may or may not be the same object as _writer_, depending on whether
	# it is a (single-duplex) pipe or a (full-duplex) socket.
	attr_reader :reader
	
	# An IO object, used for writing data to the application instance.
	# This, together with _reader_, is the application's main communication
	# channel with the outside world.
	#
	# This may or may not be the same object as _reader_, depending on whether
	# it is a (single-duplex) pipe or a (full-duplex) socket.
	attr_reader :writer

	# Return the Ruby on Rails version that the application requires, or nil
	# if it doesn't require a particular version.
	def self.get_framework_version(app_root)
		File.read("#{app_root}/environment.rb") =~ /^[^#]*RAILS_GEM_VERSION\s+=\s+'([\d.]+)'/
		return $1
	end

	# Creates a new instance of Application. The parameters correspond with the attributes
	# of the same names. No exceptions will be thrown.
	def initialize(app_root, pid, reader, writer)
		@app_root = app_root
		@pid = pid
		@reader = reader
		@writer = writer
	end
	
	# Close the application's communication channels, i.e. close _reader_ and _writer_.
	def close
		@reader.close
		if @reader != @writer
			@writer.close
		end
	end
end

end # module ModRails