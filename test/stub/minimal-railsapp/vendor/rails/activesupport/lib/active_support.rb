class Object
	def require_dependency(name)
		require(name)
	end
end

class NilClass
	def blank?
		return true
	end
end

class String
	def blank?
		return strip.empty?
	end
end
