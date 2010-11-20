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

require 'phusion_passenger/constants'
require 'digest/md5'

module PhusionPassenger

module Rails3Extensions
	def self.init!(options)
		if !AnalyticsLogging.install!(options)
			# Remove code to save memory.
			PhusionPassenger::Rails3Extensions.send(:remove_const, :AnalyticsLogging)
			PhusionPassenger.send(:remove_const, :Rails3Extensions)
		end
	end
end

module Rails3Extensions
class AnalyticsLogging < ActiveSupport::LogSubscriber
	def self.install!(options)
		analytics_logger = options["analytics_logger"]
		return false if !analytics_logger || !options["analytics"]
		
		# If the Ruby interpreter supports GC statistics then turn it on
		# so that the info can be logged.
		GC.enable_stats if GC.respond_to?(:enable_stats)
		
		subscriber = self.new
		AnalyticsLogging.attach_to(:action_controller, subscriber)
		AnalyticsLogging.attach_to(:active_record, subscriber)
		if defined?(ActiveSupport::Cache::Store)
			ActiveSupport::Cache::Store.instrument = true
			AnalyticsLogging.attach_to(:active_support, subscriber)
		end
		
		if defined?(ActionDispatch::ShowExceptions)
			Rails.application.middleware.insert_after(
				ActionDispatch::ShowExceptions,
				ExceptionLogger, analytics_logger)
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
	
	def process_action(event)
		log = Thread.current[PASSENGER_ANALYTICS_WEB_LOG]
		if log
			view_runtime = event.payload[:view_runtime]
			log.message("View rendering time: #{(view_runtime * 1000).to_i}") if view_runtime
		end
	end
	
	def sql(event)
		log = Thread.current[PASSENGER_ANALYTICS_WEB_LOG]
		if log
			name = event.payload[:name]
			sql = event.payload[:sql]
			digest = Digest::MD5.hexdigest("#{name}\0#{sql}\0#{rand}")
			log.measured_time_points("DB BENCHMARK: #{digest}",
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
		def initialize(app, analytics_logger)
			@app = app
			@analytics_logger = analytics_logger
		end
		
		def call(env)
			@app.call(env)
		rescue Exception => e
			log_analytics_exception(env, e) if env[PASSENGER_TXN_ID]
			raise e
		end
	
	private
		def log_analytics_exception(env, exception)
			log = @analytics_logger.new_transaction(
				env[PASSENGER_GROUP_NAME],
				:exceptions,
				env[PASSENGER_UNION_STATION_KEY])
			begin
				request = ActionDispatch::Request.new(env)
				controller = request.parameters['controller'].humanize + "Controller"
				action = request.parameters['action']
				
				request_txn_id = env[PASSENGER_TXN_ID]
				message = exception.message
				message = exception.to_s if message.empty?
				message = [message].pack('m')
				message.gsub!("\n", "")
				backtrace_string = [exception.backtrace.join("\n")].pack('m')
				backtrace_string.gsub!("\n", "")
				if action
					controller_action = "#{controller}##{action}"
				else
					controller_action = controller
				end
				
				log.message("Request transaction ID: #{request_txn_id}")
				log.message("Message: #{message}")
				log.message("Class: #{exception.class.name}")
				log.message("Backtrace: #{backtrace_string}")
				log.message("Controller action: #{controller_action}")
			ensure
				log.close
			end
		end
	end
	
	module ACExtension
		def process_action(action, *args)
			log = request.env[PASSENGER_ANALYTICS_WEB_LOG]
			if log
				log.message("Controller action: #{self.class.name}##{action_name}")
				log.measure("framework request processing") do
					super
				end
			else
				super
			end
		end
		
		def render(*args)
			log = request.env[PASSENGER_ANALYTICS_WEB_LOG]
			if log
				log.measure("view rendering") do
					super
				end
			else
				super
			end
		end
	end
	
	module ASBenchmarkableExtension
		def benchmark_with_passenger(message = "Benchmarking", *args)
			log = Thread.current[PASSENGER_ANALYTICS_WEB_LOG]
			if log
				log.measure("BENCHMARK: #{message}") do
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
end # class AnalyticsLogging
end # module Rails3Extensions

end # module PhusionPassenger
