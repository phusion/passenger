module PhusionPassenger
module Railz
module AnalyticsLogging

module ActionControllerRescueExtension
	@@analytics_logger = nil
	
	def self.analytics_logger=(logger)
		@@analytics_logger = logger
	end
	
	def rescue_action_with_passenger(exception)
		if request.env["PASSENGER_TXN_ID"]
			log = @@analytics_logger.continue_transaction(
				request.env["PASSENGER_GROUP_NAME"],
				request.env["PASSENGER_TXN_ID"],
				:exceptions, true)
			begin
				message = exception.message
				message = exception.to_s if message.empty?
				message = [message].pack('m')
				message.gsub!("\n", "")
				backtrace_string = [exception.backtrace.join("\n")].pack('m')
				backtrace_string.gsub!("\n", "")
				
				log.message("Message: #{message}")
				log.message("Class: #{exception.class.name}")
				log.message("Backtrace: #{backtrace_string}")
			ensure
				log.close
			end
		end
		rescue_action_without_passenger(exception)
	end
end

end
end
end