RAILS_FRAMEWORK_ROOT = File.expand_path("#{File.dirname(__FILE__)}/../..")

module Rails
	class Initializer
		attr_accessor :configuration
		
		def self.run(action = :boot)
			inst = self.new
			if inst.respond_to?(action)
				inst.send(action)
			end
		end
		
		def initialize
			@configuration = Configuration.new
		end
		
		def boot
			set_load_path
			load_environment
		end
		
		def load_environment
			require "#{RAILS_FRAMEWORK_ROOT}/activesupport/lib/active_support"
			require "#{RAILS_FRAMEWORK_ROOT}/actionpack/lib/action_controller"
		end
		
		def set_load_path
			if defined?(RAILS_ROOT)
				$LOAD_PATH << "#{RAILS_ROOT}/app/controllers"
			end
			$LOAD_PATH << "#{RAILS_FRAMEWORK_ROOT}/railties/lib"
		end
	
	protected
		class Configuration
			attr_accessor :log_path
			attr_accessor :default_log_path
			
			def initialize
				@log_path = @default_log_path = 'foo.log'
			end
		end
	end
	
	class GemDependency
		def self.add_frozen_gem_path
		end
	end
	
	module VERSION
		MAJOR = 2
		MINOR = 3
		TINY = 4
		STRING = [MAJOR, MINOR, TINY].join('.')
	end
end
