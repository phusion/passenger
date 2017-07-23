#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2010-2017 Phusion Holding B.V.
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

PhusionPassenger.require_passenger_lib 'utils'   # So that we can know whether #writev is supported.

module PhusionPassenger
  module Utils

    # Some frameworks (e.g. Merb) call `seek` and `rewind` on the input stream
    # if it responds to these methods. In case of Phusion Passenger, the input
    # stream is a socket, and altough socket objects respond to `seek` and
    # `rewind`, calling these methods will raise an exception. We don't want
    # this to happen so in AbstractRequestHandler we wrap the client socket
    # into an UnseekableSocket wrapper, which doesn't respond to these methods.
    #
    # We used to dynamically undef `seek` and `rewind` on sockets, but this
    # blows the Ruby interpreter's method cache and made things slower.
    # Wrapping a socket is faster despite extra method calls.
    #
    # Furthermore, all exceptions originating from the wrapped socket will
    # be annotated. One can check whether a certain exception originates
    # from the wrapped socket by calling #source_of_exception?
    class UnseekableSocket
      def self.wrap(socket)
        return new.wrap(socket)
      end

      def wrap(socket)
        # Some people report that sometimes their Ruby (MRI/REE)
        # processes get stuck with 100% CPU usage. Upon further
        # inspection with strace, it turns out that these Ruby
        # processes are continuously calling lseek() on a socket,
        # which of course returns ESPIPE as error. gdb reveals
        # lseek() is called by fwrite(), which in turn is called
        # by rb_fwrite(). The affected socket is the
        # AbstractRequestHandler client socket.
        #
        # I inspected the MRI source code and didn't find
        # anything that would explain this behavior. This makes
        # me think that it's a glibc bug, but that's very
        # unlikely.
        #
        # The rb_fwrite() implementation takes an entirely
        # different code path if I set 'sync' to true: it will
        # skip fwrite() and use write() instead. So here we set
        # 'sync' to true in the hope that this will work around
        # the problem.
        socket.sync = true

        # There's no need to set the encoding for Ruby 1.9 because
        # abstract_request_handler.rb is tagged with 'encoding: binary'.

        @socket = socket

        return self
      end

      # Don't allow disabling of sync.
      def sync=(value)
      end

      # Socket is sync'ed so flushing shouldn't do anything.
      def flush
      end

      # Already set to binary mode.
      def binmode
      end

      # This makes select() work.
      def to_io
        @socket
      end

      def simulate_eof!
        @simulate_eof = true
      end

      def stop_simulating_eof!
        @simulate_eof = false
      end

      def fileno
        @socket.fileno
      end

      def addr
        @socket.addr
      rescue => e
        raise annotate(e)
      end

      def write(string)
        @socket.write(string)
      rescue => e
        raise annotate(e)
      end

      def write_nonblock(string, *args)
        @socket.write_nonblock(string, *args)
      rescue => e
        raise annotate(e)
      end

      def writev(components)
        @socket.writev(components)
      rescue => e
        raise annotate(e)
      end if IO.method_defined?(:writev)

      def writev2(components, components2)
        @socket.writev2(components, components2)
      rescue => e
        raise annotate(e)
      end if IO.method_defined?(:writev2)

      def writev3(components, components2, components3)
        @socket.writev3(components, components2, components3)
      rescue => e
        raise annotate(e)
      end if IO.method_defined?(:writev3)

      def send(*args)
        @socket.send(*args)
      rescue => e
        raise annotate(e)
      end

      def sendmsg(*args)
        @socket.sendmsg(*args)
      rescue => e
        raise annotate(e)
      end

      def sendmsg_nonblock(*args)
        @socket.sendmsg_nonblock(*args)
      rescue => e
        raise annotate(e)
      end

      def puts(*args)
        @socket.puts(*args)
      rescue => e
        raise annotate(e)
      end

      def gets
        return nil if @simulate_eof
        @socket.gets
      rescue => e
        raise annotate(e)
      end

      def read(*args)
        if @simulate_eof
          length, buffer = args
          if buffer
            buffer.replace(binary_string(""))
          else
            buffer = binary_string("")
          end
          if length
            return nil
          else
            return buffer
          end
        end
        @socket.read(*args)
      rescue => e
        raise annotate(e)
      end

      def read_nonblock(*args)
        raise EOFError, "end of file reached" if @simulate_eof
        @socket.read_nonblock(*args)
      rescue => e
        raise annotate(e)
      end

      def readpartial(*args)
        raise EOFError, "end of file reached" if @simulate_eof
        @socket.readpartial(*args)
      rescue => e
        raise annotate(e)
      end

      def readline
        raise EOFError, "end of file reached" if @simulate_eof
        @socket.readline
      rescue => e
        raise annotate(e)
      end

      def recv(*args)
        raise EOFError, "end of file reached" if @simulate_eof
        @socket.recv(*args)
      rescue => e
        raise annotate(e)
      end

      def recvfrom(*args)
        raise EOFError, "end of file reached" if @simulate_eof
        @socket.recvfrom(*args)
      rescue => e
        raise annotate(e)
      end

      def recvfrom_nonblock(*args)
        raise EOFError, "end of file reached" if @simulate_eof
        @socket.recvfrom_nonblock(*args)
      rescue => e
        raise annotate(e)
      end

      def each(&block)
        return if @simulate_eof
        @socket.each(&block)
      rescue => e
        raise annotate(e)
      end

      def eof?
        return true if @simulate_eof
        @socket.eof?
      rescue => e
        raise annotate(e)
      end

      def closed?
        @socket.closed?
      rescue => e
        raise annotate(e)
      end

      def close
        @socket.close
      rescue => e
        raise annotate(e)
      end

      def close_read
        @socket.close_read
      rescue => e
        raise annotate(e)
      end

      def close_write
        @socket.close_write
      rescue => e
        raise annotate(e)
      end

      def source_of_exception?(exception)
        return exception.instance_variable_get(:"@from_unseekable_socket") == @socket.object_id
      end

      def to_hash
        { :socket => "Not JSON Encodable", :eof => @simulate_eof }
      end

    private
      def annotate(exception)
        exception.instance_variable_set(:"@from_unseekable_socket", @socket.object_id)
        return exception
      end

      def raise_error_because_activity_disallowed!
        raise IOError, "It is not possible to read or write from the client socket because the current."
      end

      if ''.respond_to?(:force_encoding)
        def binary_string(str)
          return ''.force_encoding('binary')
        end
      else
        def binary_string(str)
          return ''
        end
      end
    end

  end # module Utils
end # module PhusionPassenger
