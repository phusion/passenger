$LOAD_PATH << File.dirname(__FILE__)

module Rails
	module VERSION
		MAJOR = 2
		MINOR = 0
		TINY = 0
		STRING = [MAJOR, MINOR, TINY].join('.')
	end
end

class Object
	def require_dependency(name)
		require(name)
	end

	def blank?
		return false
	end
end
