module PhusionPassenger
module ClassicRailsExtensions
module AnalyticsLogging

module CacheStoreExtension
	def fetch_2_1(key, options = {})
		@logger_off = true
		if !options[:force] && value = read(key, options)
			@logger_off = false
			log("hit", key, options)
			PhusionPassenger.log_cache_hit(nil, key)
			value
		elsif block_given?
			@logger_off = false
			log("miss", key, options)

			value = nil
			seconds = Benchmark.realtime { value = yield }

			@logger_off = true
			write(key, value, options)
			@logger_off = false

			log("write (will save #{'%.5f' % seconds})", key, nil)
			PhusionPassenger.log_cache_miss(nil, key, seconds * 1_000_000)

			value
		else
			PhusionPassenger.log_cache_miss(nil, key)
			value
		end
	end
	
	def fetch_2_2(key, options = {})
		@logger_off = true
		if !options[:force] && value = read(key, options)
			@logger_off = false
			log("hit", key, options)
			PhusionPassenger.log_cache_hit(nil, key)
			value
		elsif block_given?
			@logger_off = false
			log("miss", key, options)

			value = nil
			seconds = Benchmark.realtime { value = yield }

			@logger_off = true
			write(key, value, options)
			@logger_off = false

			log("write (will save #{'%.2f' % (seconds * 1000)}ms)", key, nil)
			PhusionPassenger.log_cache_miss(nil, key, seconds * 1_000_000)

			value
		else
			PhusionPassenger.log_cache_miss(nil, key)
			value
		end
	end
	
	def fetch_2_3(key, options = {})
		@logger_off = true
		if !options[:force] && value = read(key, options)
			@logger_off = false
			log("hit", key, options)
			PhusionPassenger.log_cache_hit(nil, key)
			value
		elsif block_given?
			@logger_off = false
			log("miss", key, options)

			value = nil
			ms = Benchmark.ms { value = yield }

			@logger_off = true
			write(key, value, options)
			@logger_off = false

			log('write (will save %.2fms)' % ms, key, nil)
			PhusionPassenger.log_cache_miss(nil, key, ms * 1_000)

			value
		else
			PhusionPassenger.log_cache_miss(nil, key)
			value
		end
	end
end

module ConcreteCacheStoreExtension
	def read(name, *args)
		result = super
		if !@logger_off
			if result.nil?
				PhusionPassenger.log_cache_miss(nil, name)
			else
				PhusionPassenger.log_cache_hit(nil, name)
			end
		end
		result
	end
end

end
end
end