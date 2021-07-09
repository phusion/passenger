#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2021 Phusion Holding B.V.
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

PhusionPassenger.require_passenger_lib 'platform_info'
PhusionPassenger.require_passenger_lib 'platform_info/operating_system'

module PhusionPassenger

  module PlatformInfo
    def self.pcre_extra_cflags
      if PlatformInfo.os_name_simple == "macosx"
        if File.exist?("/usr/local/opt/pcre/include")
          # Use the one from Homebrew.
          "-I/usr/local/opt/pcre/include"
        elsif File.exist?("/opt/homebrew/opt/pcre/include")
          # Use the one from Homebrew on Apple Silicon
          "-I/opt/homebrew/opt/pcre/include"
        else
          # Use the one from Macports.
          "-I/opt/local/include"
        end
      else
        ""
      end
    end
    memoize :pcre_extra_cflags

    def self.pcre_extra_ldflags
      if PlatformInfo.os_name_simple == "macosx"
        if File.exist?("/usr/local/opt/pcre/include")
          "-L/usr/local/opt/pcre/lib"
        elsif File.exist?("/opt/homebrew/opt/pcre/include")
          "-L/opt/homebrew/opt/pcre/include"
        else
          "-L/opt/local/lib"
        end
      else
        ""
      end
    end
    memoize :pcre_extra_ldflags
  end

end # module PhusionPassenger
