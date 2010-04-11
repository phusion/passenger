module PhusionPassenger
module Railz
module FrameworkExtensions
module AnalyticsLogging

module ARAbstractAdapterExtension
protected
	def log_with_passenger(sql, name, &block)
		# Log SQL queries and durations.
		log = Thread.current[PASSENGER_ANALYTICS_WEB_LOG]
		if log
			sql_base64 = [sql].pack("m")
			sql_base64.strip!
			log.measure("DB BENCHMARK: #{sql_base64} #{name}") do
				log_without_passenger(sql, name, &block)
			end
		else
			log_without_passenger(sql, name, &block)
		end
	end
end

end
end
end
end