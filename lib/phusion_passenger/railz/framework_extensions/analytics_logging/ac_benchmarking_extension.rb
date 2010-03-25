module PhusionPassenger
module Railz
module FrameworkExtensions
module AnalyticsLogging

module ACBenchmarkingExtension
	def benchmark_with_passenger(title, *args)
		log = Thread.current[PASSENGER_ANALYTICS_WEB_LOG]
		if log
			log.measure("BENCHMARK: #{title}") do
				benchmark_without_passenger(title, *args) do
					yield
				end
			end
		else
			benchmark_without_passenger(title, *args, &block)
		end
	end
end

end
end
end
end