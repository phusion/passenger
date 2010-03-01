module PhusionPassenger
module Railz
module FrameworkExtensions
module AnalyticsLogging

module ACRescueExtension
protected
	def rescue_action_with_passenger(exception)
		# When a controller action crashes, log the exception.
		# But ignore routing errors (404s and stuff).
		if !defined?(ActionController::RoutingError) || !exception.is_a?(ActionController::RoutingError)
			AnalyticsLogging.continue_transaction_logging(request, :exceptions, true) do |log|
				message = exception.message
				message = exception.to_s if message.empty?
				message = [message].pack('m')
				message.gsub!("\n", "")
				backtrace_string = [exception.backtrace.join("\n")].pack('m')
				backtrace_string.gsub!("\n", "")
				
				log.message("Message: #{message}")
				log.message("Class: #{exception.class.name}")
				log.message("Backtrace: #{backtrace_string}")
				log.message("Controller action: #{controller_class_name}##{action_name}")
			end
		end
		rescue_action_without_passenger(exception)
	end
end

end
end
end
end