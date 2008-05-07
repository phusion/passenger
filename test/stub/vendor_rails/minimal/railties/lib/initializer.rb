RAILS_FRAMEWORK_ROOT = File.expand_path("#{File.dirname(__FILE__)}/../..")

module Rails
	class Initializer
		def self.run(action = :boot)
			inst = self.new
			if inst.respond_to?(action)
				inst.send(action)
			end
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
		end
	end
	
	module VERSION
		MAJOR = 2
		MINOR = 0
		TINY = 0
		STRING = [MAJOR, MINOR, TINY].join('.')
	end
end
