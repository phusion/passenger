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
	# Set during spawning, and set back to nil when spawning is done.
	attr_accessor :_spawn_options
	
	@@event_starting_worker_process = []
	@@event_stopping_worker_process = []
	@@event_credentials = []
	@@event_after_installing_signal_handlers = []
	
	def on_event(name, &block)
		callback_list_for_event(name) << block
	end
	
	def call_event(name, *args)
		callback_list_for_event(name).each do |callback|
			callback.call(*args)
		end
	end
	
	def install_framework_extensions!(*args)
		require 'rails/version' if defined?(::Rails) && !defined?(::Rails::VERSION)
		if defined?(::Rails) && ::Rails::VERSION::MAJOR == 3
			require 'phusion_passenger/rails3_extensions/init'
			Rails3Extensions.init!(_spawn_options, *args)
		end
	end
	
	def benchmark(env = nil, title = "Benchmarking")
		log = lookup_analytics_log(env)
		if log
			log.measure("BENCHMARK: #{title}") do
				yield
			end
		else
			yield
		end
	end
	
	def log_cache_hit(env, name)
		log = lookup_analytics_log(env)
		if log
			log.message("Cache hit: #{name}")
			return true
		else
			return false
		end
	end
	
	def log_cache_miss(env, name, generation_time = nil)
		log = lookup_analytics_log(env)
		if log
			if generation_time
				log.message("Cache miss (#{generation_time.to_i}): #{name}")
			else
				log.message("Cache miss: #{name}")
			end
			return true
		else
			return false
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
		when :after_installing_signal_handlers
			@@event_after_installing_signal_handlers
		else
			raise ArgumentError, "Unknown event name '#{name}'"
		end
	end
	
	def lookup_analytics_log(env)
		if env
			return env[PASSENGER_ANALYTICS_WEB_LOG]
		else
			return Thread.current[PASSENGER_ANALYTICS_WEB_LOG]
		end
	end

end
end # module PhusionPassenger
