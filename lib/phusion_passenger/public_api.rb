#  Phusion Passenger - http://www.modrails.com/
#  Copyright (c) 2010 Phusion
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
class << self
	@@event_starting_worker_process = []
	@@event_stopping_worker_process = []
	@@event_credentials = []
	
	def on_event(name, &block)
		callback_list_for_event(name) << block
	end
	
	def call_event(name, *args)
		callback_list_for_event(name).each do |callback|
			callback.call(*args)
		end
	end
	
	def benchmark(env = nil, title = "Benchmarking")
		if env
			log = env[PASSENGER_ANALYTICS_WEB_LOG]
		else
			log = Thread.current[PASSENGER_ANALYTICS_WEB_LOG]
		end
		if log
			log.measure("BENCHMARK: #{title}") do
				yield
			end
		else
			yield
		end
	end

private
	def callback_list_for_event(name)
		return case name
		when :starting_worker_process
			@@event_starting_worker_process
		when :stopping_worker_process
			@@event_stopping_worker_process
		when :credentials
			@@event_credentials
		else
			raise ArgumentError, "Unknown event name '#{name}'"
		end
	end

end
end # module PhusionPassenger
