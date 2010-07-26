#  Phusion Passenger - http://www.modrails.com/
#  Copyright (c) 2010 Phusion
#
#  "Phusion Passenger" is a trademark of Hongli Lai & Ninh Bui.
#
#  Permission is hereby granted, free of charge, to any person obtaining a copy
#  of this software and associated documentation files (the "Software"), to deal
#  in the Software without restriction, including without limitation the rights
#  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
#  copies of the Software, and to permit persons to whom the Software is
#  furnished to do so, subject to the following conditions:
#
#  The above copyright notice and this permission notice shall be included in
#  all copies or substantial portions of the Software.
#
#  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
#  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
#  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
#  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
#  THE SOFTWARE.

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