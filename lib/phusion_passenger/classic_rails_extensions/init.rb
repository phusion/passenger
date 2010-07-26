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

require 'phusion_passenger/constants'

module PhusionPassenger
module ClassicRailsExtensions
	def self.init!(options)
		if options["analytics_logger"] && options["analytics"]
			AnalyticsLogging.install!(options)
		else
			# Remove code to save memory.
			PhusionPassenger.send(:remove_const, :ClassicRailsExtensions)
		end
	end
end
end

module PhusionPassenger
module ClassicRailsExtensions
module AnalyticsLogging
	# Instantiated from prepare_app_process in utils.rb.
	@@analytics_logger = nil
	
	def self.install!(options)
		@@analytics_logger = options["analytics_logger"]
		# If the Ruby interpreter supports GC statistics then turn it on
		# so that the info can be logged.
		GC.enable_stats if GC.respond_to?(:enable_stats)
		
		if defined?(ActionController)
			require 'phusion_passenger/classic_rails_extensions/analytics_logging/ac_base_extension'
			ActionController::Base.class_eval do
				include ACBaseExtension
				alias_method_chain :perform_action, :passenger
				alias_method_chain :render, :passenger
			end
			
			require 'phusion_passenger/classic_rails_extensions/analytics_logging/ac_benchmarking_extension'
			ActionController::Benchmarking::ClassMethods.class_eval do
				include ACBenchmarkingExtension
				alias_method_chain :benchmark, :passenger
			end
			
			require 'phusion_passenger/classic_rails_extensions/analytics_logging/ac_rescue_extension'
			ActionController::Rescue.class_eval do
				include ACRescueExtension
				alias_method_chain :rescue_action, :passenger
			end
		end
		
		if defined?(ActionView)
			require 'phusion_passenger/classic_rails_extensions/analytics_logging/av_benchmark_helper_extension'
			ActionView::Helpers::BenchmarkHelper.class_eval do
				include AVBenchmarkHelperExtension
				alias_method_chain :benchmark, :passenger
			end
		end
		
		if defined?(ActiveRecord)
			require 'phusion_passenger/classic_rails_extensions/analytics_logging/ar_abstract_adapter_extension'
			ActiveRecord::ConnectionAdapters::AbstractAdapter.class_eval do
				include ARAbstractAdapterExtension
				alias_method_chain :log, :passenger
			end
		end
		
		if defined?(ActiveSupport::Cache::Store) && Rails.cache
			require 'phusion_passenger/classic_rails_extensions/analytics_logging/as_cache_extension'
			ActiveSupport::Cache::Store.class_eval do
				include CacheStoreExtension
				case Rails::VERSION::MINOR
				when 1
					# Rails 2.1
					alias_method :fetch, :fetch_2_1
				when 2
					# Rails 2.2
					alias_method :fetch, :fetch_2_2
				when 3
					# Rails 2.3
					alias_method :fetch, :fetch_2_3
				end
			end
			Rails.cache.extend(ConcreteCacheStoreExtension)
		end
	end
	
	def self.new_transaction_log(env, category = :requests)
		if env[PASSENGER_TXN_ID]
			group_name = env[PASSENGER_GROUP_NAME]
			union_station_key = env[PASSENGER_UNION_STATION_KEY]
			log = @@analytics_logger.new_transaction(group_name, category,
				union_station_key)
			begin
				yield log
			ensure
				log.close
			end
		end
	end
end
end
end