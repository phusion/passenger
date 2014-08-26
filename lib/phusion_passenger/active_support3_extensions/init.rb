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
		super()
		install_event_preprocessor(options[:event_preprocessor]) if options[:event_preprocessor]
	end
	
	def process_action(event)
		view_runtime = event.payload[:view_runtime]
		if view_runtime
			PhusionPassenger.log_total_view_rendering_time(nil, (view_runtime * 1000).to_i)
		end
	end
	
	def sql(event)
		PhusionPassenger.log_database_query(nil,
			event.payload[:name] || "SQL",
			event.time,
			event.end,
			event.payload[:sql])
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
			options = {}
			request = ActionDispatch::Request.new(env)
			if request.parameters['controller']
				options[:controller_name] = request.parameters['controller'].humanize + "Controller"
				options[:action_name] = request.parameters['action']
			end
			PhusionPassenger.log_request_exception(env, exception, options)
		end
	end
	
	module ACExtension
		def process_action(action, *args)
			options = {
				:controller_name => self.class.name,
				:action_name => action_name,
				:method => request.request_method
			}
			PhusionPassenger.log_controller_action(request.env, options) do
				super
			end
		end
		
		def render(*args)
			PhusionPassenger.log_view_rendering(request.env) do
				super
			end
		end
	end
	
	module ASBenchmarkableExtension
		def benchmark_with_passenger(message = "Benchmarking", *args)
			PhusionPassenger.benchmark(nil, message) do
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
