# encoding: binary
#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2010-2018 Phusion Holding B.V.
#
#  "Passenger", "Phusion Passenger" and "Union Station" are registered
#  trademarks of Phusion Holding B.V.
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

  # This class allows reading and writing structured messages over
  # I/O channels. This is the Ruby implementation of src/cxx_supportlib/IOTools/MessageIO.h;
  # see that file for more information.
  class MessageChannel
    HEADER_SIZE = 2                  # :nodoc:
    DELIMITER = "\0"                 # :nodoc:
    DELIMITER_NAME = "null byte"     # :nodoc:
    UINT16_PACK_FORMAT = "n"         # :nodoc:
    UINT32_PACK_FORMAT = "N"         # :nodoc:

    class InvalidHashError < StandardError
    end

    # The wrapped IO object.
    attr_accessor :io

    # Create a new MessageChannel by wrapping the given IO object.
    def initialize(io = nil)
      @io = io
      # Make it binary just in case.
      @io.binmode if @io
    end

    # Read an array message from the underlying file descriptor.
    # Returns the array message as an array, or nil when end-of-stream has
    # been reached.
    #
    # Might raise SystemCallError, IOError or SocketError when something
    # goes wrong.
    def read
      buffer = new_buffer
      if !@io.read(HEADER_SIZE, buffer)
        return nil
      end
      while buffer.size < HEADER_SIZE
        tmp = @io.read(HEADER_SIZE - buffer.size)
        if tmp.empty?
          return nil
        else
          buffer << tmp
        end
      end

      chunk_size = buffer.unpack(UINT16_PACK_FORMAT)[0]
      if !@io.read(chunk_size, buffer)
        return nil
      end
      while buffer.size < chunk_size
        tmp = @io.read(chunk_size - buffer.size)
        if tmp.empty?
          return nil
        else
          buffer << tmp
        end
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
    end

    # Read an array message from the underlying file descriptor and return the
    # result as a hash instead of an array. This assumes that the array message
    # has an even number of elements.
    # Returns nil when end-of-stream has been reached.
    #
    # Might raise SystemCallError, IOError or SocketError when something
    # goes wrong.
    def read_hash
      buffer = new_buffer
      if !@io.read(HEADER_SIZE, buffer)
        return nil
      end
      while buffer.size < HEADER_SIZE
        tmp = @io.read(HEADER_SIZE - buffer.size)
        if tmp.empty?
          return nil
        else
          buffer << tmp
        end
      end

      chunk_size = buffer.unpack(UINT16_PACK_FORMAT)[0]
      if !@io.read(chunk_size, buffer)
        return nil
      end
      while buffer.size < chunk_size
        tmp = @io.read(chunk_size - buffer.size)
        if tmp.empty?
          return nil
        else
          buffer << tmp
        end
      end

      result = {}
      offset = 0
      delimiter_pos = buffer.index(DELIMITER, offset)
      while !delimiter_pos.nil?
        if delimiter_pos == 0
          name = ""
        else
          name = buffer[offset .. delimiter_pos - 1]
        end

        offset = delimiter_pos + 1
        delimiter_pos = buffer.index(DELIMITER, offset)
        if delimiter_pos.nil?
          raise InvalidHashError
        elsif delimiter_pos == 0
          value = ""
        else
          value = buffer[offset .. delimiter_pos - 1]
        end

        result[name] = value
        offset = delimiter_pos + 1
        delimiter_pos = buffer.index(DELIMITER, offset)
      end
      return result
    rescue Errno::ECONNRESET
      return nil
    end

    # Read a scalar message from the underlying IO object. Returns the
    # read message, or nil on end-of-stream.
    #
    # Might raise SystemCallError, IOError or SocketError when something
    # goes wrong.
    #
    # The +buffer+ argument specifies a buffer in which #read_scalar
    # stores the read data. It is good practice to reuse existing buffers
    # in order to minimize stress on the garbage collector.
    #
    # The +max_size+ argument allows one to specify the maximum allowed
    # size for the scalar message. If the received scalar message's size
    # is larger than +max_size+, then a SecurityError will be raised.
    def read_scalar(buffer = new_buffer, max_size = nil)
      if !@io.read(4, buffer)
        return nil
      end
      while buffer.size < 4
        tmp = @io.read(4 - buffer.size)
        if tmp.empty?
          return nil
        else
          buffer << tmp
        end
      end

      size = buffer.unpack(UINT32_PACK_FORMAT)[0]
      if size == 0
        buffer.replace('')
        return buffer
      else
        if !max_size.nil? && size > max_size
          raise SecurityError, "Scalar message size (#{size}) " <<
            "exceeds maximum allowed size (#{max_size})."
        end
        if !@io.read(size, buffer)
          return nil
        end
        if buffer.size < size
          tmp = ''
          while buffer.size < size
            if !@io.read(size - buffer.size, tmp)
              return nil
            else
              buffer << tmp
            end
          end
        end
        return buffer
      end
    rescue Errno::ECONNRESET
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

      if message.size > 2 ** 16 - 1
        raise ArgumentError, 'Message size too large'
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

    # Checks whether the underlying IO stream is closed.
    def closed?
      return @io.closed?
    end

  private
    def check_argument(arg)
      if arg.to_s.index(DELIMITER)
        raise ArgumentError, "Message name and arguments may not contain #{DELIMITER_NAME}."
      end
    end

    if defined?(ByteString)
      def new_buffer
        return ByteString.new
      end
    else
      def new_buffer
        return ""
      end
    end
  end

end # module PhusionPassenger
