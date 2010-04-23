require 'phusion_passenger/constants'

module PhusionPassenger
module ClassicRailsExtensions
	def self.init!(options)
		if options["analytics_logger"]
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
	end
	
	def self.new_transaction_log(env, category = :requests)
		if env[PASSENGER_TXN_ID]
			group_name = env[PASSENGER_GROUP_NAME]
			log = @@analytics_logger.new_transaction(group_name, category)
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