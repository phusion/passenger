# encoding: binary
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

module PhusionPassenger

# This class provides convenience methods for:
# - sending and receiving raw data over an IO channel.
# - sending and receiving messages over an IO channel.
# - file descriptor (IO object) passing over a Unix socket.
# All of these methods use exceptions for error reporting.
#
# There are two kinds of messages:
# [ Array messages ]
#   These are just a list of strings, and the message
#   itself has a specific length. The contained strings may not
#   contain NUL characters (<tt>'\\0'</tt>). Note that an array message
#   must have at least one element.
# [ Scalar messages ]
#   These are byte strings which may contain arbitrary
#   binary data. Scalar messages also have a specific length.
#
# The protocol is designed to be low overhead, easy to implement and
# easy to parse.
#
# MessageChannel is to be wrapped around an IO object. For example:
#
#  a, b = IO.pipe
#  channel1 = MessageChannel.new(a)
#  channel2 = MessageChannel.new(b)
#  
#  # Send an array message.
#  channel2.write("hello", "world !!")
#  channel1.read    # => ["hello", "world !!"]
#  
#  # Send a scalar message.
#  channel2.write_scalar("some long string which can contain arbitrary binary data")
#  channel1.read_scalar
#
# The life time of a MessageChannel is independent from that of the
# wrapped IO object. If a MessageChannel object is destroyed,
# the underlying IO object is not automatically closed. Call close()
# if you want to close the underlying IO object.
#
# Note:
# Be careful with mixing the sending/receiving of array messages,
# scalar messages and IO objects. If you send a collection of any
# of these in a specific order, then the receiving side must receive them
# in the exact some order. So suppose you first send a message, then an
# IO object, then a scalar, then the receiving side must first
# receive a message, then an IO object, then a scalar. If the
# receiving side does things in the wrong order then bad things will
# happen.
class MessageChannel
	HEADER_SIZE = 2                  # :nodoc:
	DELIMITER = "\0"                 # :nodoc:
	DELIMITER_NAME = "null byte"     # :nodoc:
	
	# The wrapped IO object.
	attr_reader :io

	# Create a new MessageChannel by wrapping the given IO object.
	def initialize(io)
		@io = io
	end
	
	# Read an array message from the underlying file descriptor.
	# Returns the array message as an array, or nil when end-of-stream has
	# been reached.
	#
	# Might raise SystemCallError, IOError or SocketError when something
	# goes wrong.
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
		
		message = []
		offset = 0
		delimiter_pos = buffer.index(DELIMITER, offset)
		while !delimiter_pos.nil?
			if delimiter_pos == 0
				message << ""
			else
				message << buffer[offset .. delimiter_pos - 1]
			end
			offset = delimiter_pos + 1
			delimiter_pos = buffer.index(DELIMITER, offset)
		end
		return message
	rescue Errno::ECONNRESET
		return nil
	rescue EOFError
		return nil
	end

	# Read a scalar message from the underlying IO object. Returns the
	# read message, or nil on end-of-stream.
	#
	# Might raise SystemCallError, IOError or SocketError when something
	# goes wrong.
	#
	# The +max_size+ argument allows one to specify the maximum allowed
	# size for the scalar message. If the received scalar message's size
	# is larger than +max_size+, then a SecurityError will be raised.
	def read_scalar(max_size = nil)
		buffer = ''
		temp = ''
		while buffer.size < 4
			buffer << @io.readpartial(4 - buffer.size, temp)
		end
		size = buffer.unpack('N')[0]
		if size == 0
			return ''
		else
			if !max_size.nil? && size > max_size
				raise SecurityError, "Scalar message size (#{size}) " <<
					"exceeds maximum allowed size (#{max_size})."
			end
			buffer = ''
			while buffer.size < size
				temp = '' # JRuby doesn't clear the buffer. TODO: remove this when JRuby has been fixed.
				buffer << @io.readpartial(size - buffer.size, temp)
			end
			return buffer
		end
	rescue Errno::ECONNRESET
		return nil
	rescue EOFError
		return nil
	end
	
	# Send an array message, which consists of the given elements, over the underlying
	# file descriptor. _name_ is the first element in the message, and _args_ are the
	# other elements. These arguments will internally be converted to strings by calling
	# to_s().
	#
	# Might raise SystemCallError, IOError or SocketError when something
	# goes wrong.
	def write(name, *args)
		check_argument(name)
		args.each do |arg|
			check_argument(arg)
		end
		
		message = "#{name}#{DELIMITER}"
		args.each do |arg|
			message << arg.to_s << DELIMITER
		end
		@io.write([message.size].pack('n') << message)
		@io.flush
	end
	
	# Send a scalar message over the underlying IO object.
	#
	# Might raise SystemCallError, IOError or SocketError when something
	# goes wrong.
	def write_scalar(data)
		@io.write([data.size].pack('N') << data)
		@io.flush
	end
	
	# Receive an IO object (a file descriptor) from the channel. The other
	# side must have sent an IO object by calling send_io(). Note that
	# this only works on Unix sockets.
	#
	# Might raise SystemCallError, IOError or SocketError when something
	# goes wrong.
	def recv_io(klass = IO, negotiate = true)
		write("pass IO") if negotiate
		io = @io.recv_io(klass)
		write("got IO") if negotiate
		return io
	end
	
	# Send an IO object (a file descriptor) over the channel. The other
	# side must receive the IO object by calling recv_io(). Note that
	# this only works on Unix sockets.
	#
	# Might raise SystemCallError, IOError or SocketError when something
	# goes wrong.
	def send_io(io)
		# We read a message before actually calling #send_io
		# in order to prevent the other side from accidentally
		# read()ing past the normal data and reading our file
		# descriptor too.
		#
		# For example suppose that side A looks like this:
		#
		#   read(fd, buf, 1024)
		#   read_io(fd)
		#
		# and side B:
		#
		#   write(fd, buf, 100)
		#   send_io(fd_to_pass)
		#
		# If B completes both write() and send_io(), then A's read() call
		# reads past the 100 bytes that B sent. On some platforms, like
		# Linux, this will cause read_io() to fail. And it just so happens
		# that Ruby's IO#read method slurps more than just the given amount
		# of bytes.
		result = read
		if !result
			raise EOFError, "End of stream"
		elsif result != ["pass IO"]
			raise IOError, "IO passing pre-negotiation header expected"
		else
			@io.send_io(io)
			# Once you've sent the IO you expect to be able to close it on the
			# sender's side, even if the other side hasn't read the IO yet.
			# Not so: on some operating systems (I'm looking at you OS X) this
			# can cause the receiving side to receive a bad file descriptor.
			# The post negotiation protocol ensures that we block until the
			# other side has really received the IO.
			result = read
			if !result
				raise EOFError, "End of stream"
			elsif result != ["got IO"]
				raise IOError, "IO passing post-negotiation header expected"
			end
		end
	end
	
	# Return the file descriptor of the underlying IO object.
	def fileno
		return @io.fileno
	end
	
	# Close the underlying IO stream. Might raise SystemCallError or
	# IOError when something goes wrong.
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

end # module PhusionPassenger
