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

PhusionPassenger.require_passenger_lib 'constants'
require 'digest/md5'

module PhusionPassenger

module ActiveSupport3Extensions
	def self.init!(options, user_options = {})
		if !UnionStationExtension.install!(options, user_options)
			# Remove code to save memory.
			PhusionPassenger::ActiveSupport3Extensions.send(:remove_const, :UnionStationExtension)
			PhusionPassenger.send(:remove_const, :ActiveSupport3Extensions)
		end
	end
end

module ActiveSupport3Extensions
class UnionStationExtension < ActiveSupport::LogSubscriber
	def self.install!(options, user_options)
		union_station_core = options["union_station_core"]
		app_group_name = options["app_group_name"]
		return false if !union_station_core || !options["analytics"]
		
		# If the Ruby interpreter supports GC statistics then turn it on
		# so that the info can be logged.
		GC.enable_stats if GC.respond_to?(:enable_stats)
		
		subscriber = self.new(user_options)
		UnionStationExtension.attach_to(:action_controller, subscriber)
		UnionStationExtension.attach_to(:active_record, subscriber)
		if defined?(ActiveSupport::Cache::Store)
			ActiveSupport::Cache::Store.instrument = true
			UnionStationExtension.attach_to(:active_support, subscriber)
		end
		PhusionPassenger.on_event(:starting_request_handler_thread) do
			if defined?(ActiveSupport::Cache::Store)
				# This flag is thread-local.
				ActiveSupport::Cache::Store.instrument = true
			end
		end
		
		if defined?(ActionDispatch::DebugExceptions)
			exceptions_middleware = ActionDispatch::DebugExceptions
		elsif defined?(ActionDispatch::ShowExceptions)
			exceptions_middleware = ActionDispatch::ShowExceptions
		end
		if exceptions_middleware
			if defined?(Rails)
				Rails.application.middleware.insert_after(
					exceptions_middleware,
					ExceptionLogger,
					union_station_core,
					app_group_name)
			end
		end
		
		if defined?(ActionController::Base)
			ActionController::Base.class_eval do
				include ACExtension
			end
		end
		
		if defined?(ActiveSupport::Benchmarkable)
			ActiveSupport::Benchmarkable.class_eval do
				include ASBenchmarkableExtension
				alias_method_chain :benchmark, :passenger
			end
		end
		
		return true
	end

	def initialize(options)
		install_event_preprocessor(options[:event_preprocessor]) if options[:event_preprocessor]
	end
	
	def process_action(event)
		transaction = Thread.current[UNION_STATION_REQUEST_TRANSACTION]
		if transaction
			view_runtime = event.payload[:view_runtime]
			transaction.message("View rendering time: #{(view_runtime * 1000).to_i}") if view_runtime
		end
	end
	
	def sql(event)
		if transaction = Thread.current[UNION_STATION_REQUEST_TRANSACTION]
			name = event.payload[:name] || "SQL"
			sql = event.payload[:sql]
			digest = Digest::MD5.hexdigest("#{name}\0#{sql}\0#{rand}")
			transaction.measured_time_points("DB BENCHMARK: #{digest}",
				event.time, event.end, "#{name}\n#{sql}")
		end
	end
	
	def cache_read(event)
		if event.payload[:hit]
			PhusionPassenger.log_cache_hit(nil, event.payload[:key])
		else
			PhusionPassenger.log_cache_miss(nil, event.payload[:key])
		end
	end
	
	def cache_fetch_hit(event)
		PhusionPassenger.log_cache_hit(nil, event.payload[:key])
	end
	
	def cache_generate(event)
		PhusionPassenger.log_cache_miss(nil, event.payload[:key],
			event.duration * 1000)
	end
	
	class ExceptionLogger
		def initialize(app, union_station_core, app_group_name)
			@app = app
			@union_station_core = union_station_core
			@app_group_name = app_group_name
		end
		
		def call(env)
			@app.call(env)
		rescue Exception => e
			log_union_station_exception(env, e) if env[PASSENGER_TXN_ID]
			raise e
		end
	
	private
		def log_union_station_exception(env, exception)
			transaction = @union_station_core.new_transaction(
				@app_group_name,
				:exceptions,
				env[PASSENGER_UNION_STATION_KEY])
			begin
				request = ActionDispatch::Request.new(env)
				if request.parameters['controller']
					controller = request.parameters['controller'].humanize + "Controller"
					action = request.parameters['action']
				end
				
				request_txn_id = env[PASSENGER_TXN_ID]
				message = exception.message
				message = exception.to_s if message.empty?
				message = [message].pack('m')
				message.gsub!("\n", "")
				backtrace_string = [exception.backtrace.join("\n")].pack('m')
				backtrace_string.gsub!("\n", "")
				if action && controller
					controller_action = "#{controller}##{action}"
				else
					controller_action = controller
				end
				
				transaction.message("Request transaction ID: #{request_txn_id}")
				transaction.message("Message: #{message}")
				transaction.message("Class: #{exception.class.name}")
				transaction.message("Backtrace: #{backtrace_string}")
				transaction.message("Controller action: #{controller_action}") if controller_action
			ensure
				transaction.close
			end
		end
	end
	
	module ACExtension
		def process_action(action, *args)
			transaction = PhusionPassenger.lookup_union_station_web_transaction(request.env)
			if transaction
				transaction.message("Controller action: #{self.class.name}##{action_name}")
				transaction.measure("framework request processing") do
					super
				end
			else
				super
			end
		end
		
		def render(*args)
			transaction = PhusionPassenger.lookup_union_station_web_transaction(request.env)
			if transaction
				transaction.measure("view rendering") do
					super
				end
			else
				super
			end
		end
	end
	
	module ASBenchmarkableExtension
		def benchmark_with_passenger(message = "Benchmarking", *args)
			transaction = PhusionPassenger.lookup_union_station_web_transaction
			if transaction
				transaction.measure("BENCHMARK: #{message}") do
					benchmark_without_passenger(message, *args) do
						yield
					end
				end
			else
				benchmark_without_passenger(message, *args) do
					yield
				end
			end
		end
	end

	private
	def install_event_preprocessor(event_preprocessor)
		public_methods(false).each do |name|
			singleton = class << self; self end
			singleton.send(:define_method, name, lambda do |event|
				event_preprocessor.call(event)
				super(event)
			end)
		end
	end
end # class UnionStationExtension
end # module ActiveSupport3Extensions

end # module PhusionPassenger
