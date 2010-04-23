require 'phusion_passenger/constants'

module PhusionPassenger
module ClassicRailsExtensions
module AnalyticsLogging

module ACRescueExtension
protected
	def rescue_action_with_passenger(exception)
		# When a controller action crashes, log the exception.
		# But ignore routing errors (404s and stuff).
		if !defined?(ActionController::RoutingError) || !exception.is_a?(ActionController::RoutingError)
			AnalyticsLogging.new_transaction_log(request.env, :exceptions) do |log|
				request_txn_id = request.env[PASSENGER_TXN_ID]
				message = exception.message
				message = exception.to_s if message.empty?
				message = [message].pack('m')
				message.gsub!("\n", "")
				backtrace_string = [exception.backtrace.join("\n")].pack('m')
				backtrace_string.gsub!("\n", "")
				
				log.message("Request transaction ID: #{request_txn_id}")
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