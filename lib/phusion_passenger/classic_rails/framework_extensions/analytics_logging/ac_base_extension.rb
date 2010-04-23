module PhusionPassenger
module ClassicRails
module FrameworkExtensions
module AnalyticsLogging

module ACBaseExtension
private
	def perform_action_with_passenger(*args)
		# Log controller and action name.
		log = request.env[PASSENGER_ANALYTICS_WEB_LOG]
		if log
			log.measure("framework request processing") do
				log.message("Controller action: #{controller_class_name}##{action_name}")
				perform_action_without_passenger(*args)
			end
		else
			perform_action_without_passenger(*args)
		end
	end
	
protected
	def render_with_passenger(*args, &block)
		log = request.env[PASSENGER_ANALYTICS_WEB_LOG]
		if log
			log.measure("view rendering") do
				render_without_passenger(*args, &block)
			end
		else
			render_without_passenger(*args, &block)
		end
	end
end

end
end
end
end