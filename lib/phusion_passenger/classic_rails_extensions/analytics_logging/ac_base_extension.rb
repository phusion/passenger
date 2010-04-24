module PhusionPassenger
module ClassicRailsExtensions
module AnalyticsLogging

module ACBaseExtension
private
	def perform_action_with_passenger(*args)
		# Log controller and action name.
		log = request.env[PASSENGER_ANALYTICS_WEB_LOG]
		if log
			log.begin_measure("framework request processing")
			begin
				log.message("Controller action: #{controller_class_name}##{action_name}")
				perform_action_without_passenger(*args)
			ensure
				log.end_measure("framework request processing",
					request.env["PASSENGER_ACTION_FAILED"])
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
				result = render_without_passenger(*args, &block)
				view_runtime = @view_runtime || @rendering_runtime
				log.message "View rendering time: #{(view_runtime * 1000).to_i}"
				return result
			end
		else
			render_without_passenger(*args, &block)
		end
	end
end

end
end
end