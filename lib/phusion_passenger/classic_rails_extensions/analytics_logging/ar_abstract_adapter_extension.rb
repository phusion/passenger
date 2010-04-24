require 'phusion_passenger/constants'
require 'digest/md5'

module PhusionPassenger
module ClassicRailsExtensions
module AnalyticsLogging

module ARAbstractAdapterExtension
protected
	def log_with_passenger(sql, name, &block)
		# Log SQL queries and durations.
		log = Thread.current[PASSENGER_ANALYTICS_WEB_LOG]
		if log
			if name
				name = name.strip
			else
				name = "SQL"
			end
			digest = Digest::MD5.hexdigest("#{name}\0#{sql}")
			log.measure("DB BENCHMARK: #{digest}", "#{name}\n#{sql}") do
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