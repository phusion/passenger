#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2014-2017 Phusion Holding B.V.
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

  class ProgressBar
    THROBBLER = ["-", "\\", "|", "/", "-"]

    def initialize(output = STDOUT)
      @output = output
      @tty = output.tty?
      @throbbler_index = 0
    end

    def set(percentage)
      if @tty
        width = (percentage * 50).to_i
        bar   = "*" * width
        space = " " * (50 - width)
        text = sprintf("[%s%s] %s", bar, space, THROBBLER[@throbbler_index])
        @throbbler_index = (@throbbler_index + 1) % THROBBLER.size
        @output.write("#{text}\r")
        @output.flush
      else
        @output.write(".")
        @output.flush
      end
    end

    def finish
      @output.write("\n")
      @output.flush
    end
  end

end # module PhusionPassenger
