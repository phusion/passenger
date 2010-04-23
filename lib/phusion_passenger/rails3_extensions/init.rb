require 'phusion_passenger/constants'

module PhusionPassenger

module Rails3Extensions
	def self.init!(options)
		if !AnalyticsLogging.install!(options)
			# Remove code to save memory.
			PhusionPassenger::Rails3Extensions.send(:remove_const, :AnalyticsLogging)
			PhusionPassenger.send(:remove_const, Rails3Extensions)
		end
	end
	
	module AnalyticsLogging
		# Instantiated from prepare_app_process in utils.rb.
		@@analytics_logger = nil
		
		def self.install!(options)
			@@analytics_logger = options["analytics_logger"]
			return false if !@@analytics_logger
			
			# If the Ruby interpreter supports GC statistics then turn it on
			# so that the info can be logged.
			GC.enable_stats if GC.respond_to?(:enable_stats)
			
			Rails::LogSubscriber.add :action_controller, LogSubscriber.new
			return true
		end
		
		class LogSubscriber < Rails::LogSubscriber
			def process_action(event)
				log = Thread.current[PASSENGER_ANALYTICS_WEB_LOG]
				if log
					view_begin = event.payload[:view_begin]
					if view_begin
						view_end = event.payload[:view_end]
						log.interval("view rendering", view_begin, view_end)
					else
						log.measured(event.payload[:view_runtime] * 1000)
					end
					
					log.message("Controller action: #{event.payload[:controller]}##{event.payload[:action]}")
				end
			end
		end
	end
end

end