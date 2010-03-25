module PhusionPassenger
module Railz
module FrameworkExtensions
module AnalyticsLogging

module AVBenchmarkHelperExtension
	def benchmark_with_passenger(message = "Benchmarking", *args)
		log = request.env[PASSENGER_ANALYTICS_WEB_LOG]
		if log
			log.measure("BENCHMARK: #{message}") do
				benchmark_without_passenger(message, *args) do |*args2|
					yield(*args2)
				end
			end
		else
			benchmark_without_passenger(message, *args) do |*args2|
				yield(*args2)
			end
		end
	end
end

end
end
end
end