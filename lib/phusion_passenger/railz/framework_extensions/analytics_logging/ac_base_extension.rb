module PhusionPassenger
module Railz
module FrameworkExtensions
module AnalyticsLogging

module ACBaseExtension
private
	def perform_action_with_passenger(*args)
		# Log controller and action name.
		log = request.env[AbstractRequestHandler::PASSENGER_ANALYTICS_WEB_LOG]
		log.message("Controller action: #{controller_class_name}##{action_name}") if log
		perform_action_without_passenger(*args)
	end
end

end
end
end
end