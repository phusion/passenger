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

module PhusionPassenger
  class << self
    @@event_starting_worker_process = []
    @@event_stopping_worker_process = []
    @@event_starting_request_handler_thread = []
    @@event_credentials = []
    @@event_after_installing_signal_handlers = []
    @@event_oob_work = []
    @@advertised_concurrency_level = nil

    def on_event(name, &block)
      callback_list_for_event(name) << block
    end

    def call_event(name, *args)
      callback_list_for_event(name).each do |callback|
        callback.call(*args)
      end
    end

    # For backward compatibility
    def install_framework_extensions!(user_options = {})
    end

    def advertised_concurrency_level
      @@advertised_concurrency_level
    end

    def advertised_concurrency_level=(value)
      @@advertised_concurrency_level = value
    end

  private
    def callback_list_for_event(name)
      return case name
      when :starting_worker_process
        @@event_starting_worker_process
      when :stopping_worker_process
        @@event_stopping_worker_process
      when :starting_request_handler_thread
        @@event_starting_request_handler_thread
      when :credentials
        @@event_credentials
      when :after_installing_signal_handlers
        @@event_after_installing_signal_handlers
      when :oob_work
        @@event_oob_work
      else
        raise ArgumentError, "Unknown event name '#{name}'"
      end
    end
  end
end # module PhusionPassenger
