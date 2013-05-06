# encoding: binary
#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2012-2013 Phusion
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

require 'thread'

module PhusionPassenger
module Rack

class OutOfBandGc
  def initialize(app, frequency, logger = nil)
    @app = app
    @frequency = frequency
    @request_count = 0
    @mutex = Mutex.new
    
    ::PhusionPassenger.on_event(:oob_work) do
      t0 = Time.now
      disabled = GC.enable
      GC.start
      GC.disable if disabled
      logger.info "Out Of Band GC finished in #{Time.now - t0} sec" if logger
    end
  end

  def call(env)
    status, headers, body = @app.call(env)

    @mutex.synchronize do
      @request_count += 1
      if @request_count == @frequency
        @request_count = 0
        headers['X-Passenger-Request-OOB-Work'] = 'true'
      end
    end
    
    [status, headers, body]
  end
end

end # module Rack
end # module PhusionPassenger

