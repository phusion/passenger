module ActionController
	class Base
		def self.page_cache_directory
			nil
		end

		def self.page_cache_directory=(dir)
		end
		
		def self.helper(*whatever)
		end
		
		def self.protect_from_forgery(*whatever)
		end
		
		def self.session(*whatever)
		end
	end

	class Dispatcher
	end
end
