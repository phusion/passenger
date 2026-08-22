#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2010-2025 Asynchronous B.V.
#
#  "Passenger", "Phusion Passenger" and "Union Station" are registered
#  trademarks of Asynchronous B.V.
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
PhusionPassenger.require_passenger_lib 'platform_info/openssl'

module PhusionPassenger

  module PlatformInfo

    def self.crypto_libs
      prefix = ' -framework CoreFoundation -framework Security'
      suffix = ' -lcrypto'
      if os_name_simple == "macosx"
        if os_version < '10.13'
          prefix
        else
          "#{prefix} #{openssl_extra_ldflags} #{suffix}"
        end
      else
        suffix
      end
    end
    memoize :crypto_libs

    def self.crypto_extra_cflags
      if os_name_simple == "macosx"
        if os_version < '10.13'
          ' -Wno-deprecated-declarations'
        else
          " -Wno-deprecated-declarations #{openssl_extra_cflags}"
        end
      else
        ''
      end
    end
    memoize :crypto_extra_cflags

  end

end # module PhusionPassenger
