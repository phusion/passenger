module PhusionPassenger
module Railz

module AnalyticsLogging
	def self.installable?(options)
		return defined?(ActionController::Rescue) &&
			ActionController::Rescue.method_defined?(:rescue_action) &&
			options["logging_agent_address"]
	end
	
	def self.install!(options)
		require 'phusion_passenger/analytics_logger'
		require 'phusion_passenger/railz/analytics_logging/action_controller_rescue_extension'
		ActionControllerRescueExtension.analytics_logger =
			AnalyticsLogger.new_from_options(options)
		ActionController::Rescue.class_eval do
			include ActionControllerRescueExtension
			alias_method_chain :rescue_action, :passenger
		end
	end
end

end
end