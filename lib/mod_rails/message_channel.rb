require 'mod_rails/native_support'
module ModRails # :nodoc:

# This class can be wrapped around an IO object to provide the ability
# to send and receive discrete messages over byte streams.
# Messages are lists of strings, with at least one element in each list.
#
# Use MessageChannel as follows:
#
#  r, w = IO.pipe
#  
#  writer_channel = MessageChannel.new(w)
#  writer_channel.write("rm", "-rf", "/")
#  writer_channel.close
#  
#  reader_channel = MessageChannel.new(r)
#  reader_channel.read    # => ["rm", "-rf", "/"]
#  reader_channel.read    # => nil (EOF)
class MessageChannel
	HEADER_SIZE = 2
	DELIMITER = "\0"
	DELIMITER_NAME = "null byte"
	
	include NativeSupport
	private :send_fd
	
	# The wrapped IO object.
	attr_reader :io

	# Create a new MessageChannel by wrapping the given IO object.
	def initialize(io)
		@io = io
	end
	
	# Read the next message from the channel. This method will block until
	# enough data for a message has been received.
	# Returns the message (a list of strings), or nil when end-of-stream has
	# been reached. If the stream is already closed, then nil will be returned
	# as well.
	#
	# What exceptions this method may raise, depends on what calling readpartial()
	# on the underlying IO object may raise. UNIXSocket objects and pipes
	# created by IO.pipe() never seem to raise any exceptions.
	def read
		buffer = ''
		while buffer.size < HEADER_SIZE
			buffer << @io.readpartial(HEADER_SIZE - buffer.size)
		end
		
		chunk_size = buffer.unpack('n')[0]
		buffer = ''
		while buffer.size < chunk_size
			buffer << @io.readpartial(chunk_size - buffer.size)
		end
		
		message = buffer.split(DELIMITER)
		if buffer[chunk_size - 1] == DELIMITER[0]
			message << ""
		end
		return message
	rescue EOFError
		return nil
	end
	
	# Write a new message into the message channel. _name_ is the first element in
	# the message name, and _args_ are the other elements. These arguments will internally
	# be converted to strings by calling to_s().
	#
	# Raises Errno::EPIPE if the other side of the IO stream has already closed the
	# connection.
	# Raises IOError if the IO stream has already been closed on this side.
	def write(name, *args)
		check_argument(name)
		args.each do |arg|
			check_argument(arg)
		end
		
		message = "#{name}"
		if !args.empty?
			message << DELIMITER << args.join(DELIMITER)
		end
		@io.write([message.size].pack('n') << message)
		@io.flush
	end
	
	# Send an IO object (a file descriptor) over the channel. The other
	# side must receive the IO object by calling recv_io(). Note that
	# this only works on Unix sockets. Please read about Unix sockets
	# file descriptor passing for more information.
	#
	# Raises Errno::EPIPE if the other side of the IO stream has already closed the
	# connection.
	# Raises IOError if the IO stream is already closed on this side.
	def send_io(io)
		if io.respond_to?(:send_io)
			@io.send_io(io)
		else
			send_fd(@io.fileno, io.fileno)
		end
	end
	
	# Receive an IO object (a file descriptor) from the channel. The other
	# side must have sent an IO object by calling send_io(). Note that
	# this only works on Unix sockets. Please read about Unix sockets
	# file descriptor passing for more information.
	#
	# Raises SocketError if the next item in the IO stream is not a file descriptor,
	# or if end-of-stream has been reached.
	# Raises IOError if the IO stream is already closed on this side.
	def recv_io
		return @io.recv_io
	end
	
	# Close the underlying IO stream. Raises IOError if the stream is already closed.
	def close
		@io.close
	end

private
	def check_argument(arg)
		if arg.to_s.index(DELIMITER)
			raise ArgumentError, "Message name and arguments may not contain #{DELIMITER_NAME}."
		end
	end
end

end # module ModRails