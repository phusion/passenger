# encoding: utf-8
#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2024 Phusion Holding B.V.
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
module Utils

# A minimal pure-Ruby StringScanner implementation so that
# PhusionPassenger::Utils::JSON doesn't have to depend on the 'strscan' gem.
class StringScanner
  attr_reader :pos, :matched

  def initialize(data)
    @data = data
    @pos = 0
    @matched = nil
  end

  def getch
    @matched =
      if @pos < @data.size
        @pos += 1
        @data[@pos - 1]
      else
        nil
      end
  end

  def scan(pattern)
    md = @data[@pos .. -1].match(/\A#{pattern}/)
    @matched =
      if md
        @pos += md[0].size
        @matched = md[0]
      else
        nil
      end
  end

  def reset
    @pos = 0
  end
end

end # module Utils
end # module PhusionPassenger
