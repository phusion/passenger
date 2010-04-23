module PhusionPassenger
module Rails3Extensions
	def self.init!(options)
		PhusionPassenger.send(:remove_const, :Rails3Extensions)
	end
end
end