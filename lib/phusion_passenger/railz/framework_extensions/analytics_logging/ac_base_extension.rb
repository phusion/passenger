module PhusionPassenger
module Railz
module FrameworkExtensions
module AnalyticsLogging

module ACBaseExtension
private
	def perform_action_with_passenger(*args)
		AnalyticsLogging.continue_transaction_logging(request) do |log|
			log.message("Controller action: #{controller_class_name}##{action_name}")
		end
		perform_action_without_passenger(*args)
	end
end

end
end
end
end