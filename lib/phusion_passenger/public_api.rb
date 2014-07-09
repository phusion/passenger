#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2010-2013 Phusion
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
	
	def install_framework_extensions!(*args)
		require 'active_support/version' if defined?(::ActiveSupport) && !defined?(::ActiveSupport::VERSION)
		if defined?(::ActiveSupport) && ::ActiveSupport::VERSION::MAJOR >= 3
			PhusionPassenger.require_passenger_lib 'active_support3_extensions/init'
			ActiveSupport3Extensions.init!(PhusionPassenger::App.options, *args)
		end
	end

	def advertised_concurrency_level
		@@advertised_concurrency_level
	end

	def advertised_concurrency_level=(value)
		@@advertised_concurrency_level = value
	end
	
	def measure_and_log_event(env, name)
		transaction = lookup_union_station_web_transaction(env)
		if transaction
			transaction.measure(name) do
				yield
			end
		else
			yield
		end
	end

	def benchmark(env = nil, title = "Benchmarking", &block)
		measure_and_log_event(env, "BENCHMARK: #{title}", &block)
	end

	# Log an exception that occurred during a request.
	def log_request_exception(env, exception, options = nil)
		return if !env[PASSENGER_TXN_ID]
		core = lookup_union_station_core(env)
		if core
			transaction = core.new_transaction(
				env[PASSENGER_APP_GROUP_NAME],
				:exceptions,
				env[PASSENGER_UNION_STATION_KEY])
			begin
				request_txn_id = env[PASSENGER_TXN_ID]
				message = exception.message
				message = exception.to_s if message.empty?
				message = [message].pack('m')
				message.gsub!("\n", "")
				backtrace_string = [exception.backtrace.join("\n")].pack('m')
				backtrace_string.gsub!("\n", "")

				transaction.message("Request transaction ID: #{request_txn_id}")
				transaction.message("Message: #{message}")
				transaction.message("Class: #{exception.class.name}")
				transaction.message("Backtrace: #{backtrace_string}")

				if options && options[:controller_name]
					if options[:action_name]
						controller_action = "#{options[:controller_name]}##{options[:action_name]}"
					else
						controller_action = controller_name
					end
					transaction.message("Controller action: #{controller_action}")
				end
			ensure
				transaction.close
			end
		end
	end

	# Log a controller action invocation.
	def log_controller_action(env, options)
		transaction = lookup_union_station_web_transaction(env)
		if transaction
			if options[:controller_name]
				if !options[:action_name]
					raise ArgumentError, "The :action_name option must be set"
				end
				transaction.message("Controller action: #{options[:controller_name]}##{options[:action_name]}")
			end
			if options[:method]
				transaction.message("Request method: #{options[:method]}")
			end
			transaction.measure("framework request processing") do
				yield
			end
		else
			yield
		end
	end

	# Log the total view rendering time of a request.
	def log_total_view_rendering_time(env, runtime)
		transaction = lookup_union_station_web_transaction(env)
		if transaction
			transaction.message("View rendering time: #{(runtime).to_i}")
		end
	end

	# Log a single view rendering.
	def log_view_rendering(env = nil, &block)
		measure_and_log_event(env, "view rendering", &block)
	end
	
	# Log a database query.
	def log_database_query(env, name, begin_time, end_time, sql)
		transaction = lookup_union_station_web_transaction(env)
		if transaction
			digest = Digest::MD5.hexdigest("#{name}\0#{sql}\0#{rand}")
			transaction.measured_time_points("DB BENCHMARK: #{digest}",
				begin_time,
				end_time,
				"#{name}\n#{sql}")
		end
	end

	def log_cache_hit(env, name)
		transaction = lookup_union_station_web_transaction(env)
		if transaction
			transaction.message("Cache hit: #{name}")
			return true
		else
			return false
		end
	end
	
	def log_cache_miss(env, name, generation_time = nil)
		transaction = lookup_union_station_web_transaction(env)
		if transaction
			if generation_time
				transaction.message("Cache miss (#{generation_time.to_i}): #{name}")
			else
				transaction.message("Cache miss: #{name}")
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

	def lookup_union_station_core(env = nil)
		if env
			result = env[UNION_STATION_CORE]
		end
		return result || Thread.current[UNION_STATION_CORE]
	end

	def lookup_union_station_web_transaction(env = nil)
		if env
			result = env[UNION_STATION_REQUEST_TRANSACTION]
		end
		return result || Thread.current[UNION_STATION_REQUEST_TRANSACTION]
	end
end
end # module PhusionPassenger
