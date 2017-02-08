# encoding: binary
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

PhusionPassenger.require_passenger_lib 'native_support'

class IO
  if defined?(PhusionPassenger::NativeSupport)
    # Writes all of the strings in the +components+ array into the given file
    # descriptor using the +writev()+ system call. Unlike IO#write, this method
    # does not require one to concatenate all those strings into a single buffer
    # in order to send the data in a single system call. Thus, #writev is a great
    # way to perform zero-copy I/O.
    #
    # Unlike the raw writev() system call, this method ensures that all given
    # data is written before returning, by performing multiple writev() calls
    # and whatever else is necessary.
    #
    #   io.writev(["hello ", "world", "\n"])
    def writev(components)
      return PhusionPassenger::NativeSupport.writev(fileno, components)
    end

    # Like #writev, but accepts two arrays. The data is written in the given order.
    #
    #   io.writev2(["hello ", "world", "\n"], ["another ", "message\n"])
    def writev2(components, components2)
      return PhusionPassenger::NativeSupport.writev2(fileno,
        components, components2)
    end

    # Like #writev, but accepts three arrays. The data is written in the given order.
    #
    #   io.writev3(["hello ", "world", "\n"],
    #     ["another ", "message\n"],
    #     ["yet ", "another ", "one", "\n"])
    def writev3(components, components2, components3)
      return PhusionPassenger::NativeSupport.writev3(fileno,
        components, components2, components3)
    end
  else
    def writev(components)
      return write(components.pack('a*' * components.size))
    end

    def writev2(components, components2)
      joined = components + components2
      return write(joined.pack('a*' * joined.size))
    end

    def writev3(components, components2, components3)
      joined = components + components2 + components3
      return write(joined.pack('a*' * joined.size))
    end
  end

  if IO.method_defined?(:close_on_exec=)
    def close_on_exec!
      begin
        self.close_on_exec = true
      rescue NotImplementedError
      end
    end
  else
    require 'fcntl'

    if defined?(Fcntl::F_SETFD)
      def close_on_exec!
        fcntl(Fcntl::F_SETFD, Fcntl::FD_CLOEXEC)
      end
    else
      def close_on_exec!
      end
    end
  end
end
