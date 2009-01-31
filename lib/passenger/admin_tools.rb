module Passenger

module AdminTools
	def self.tmpdir
		["PASSENGER_TMPDIR", "TMPDIR"].each do |name|
			if ENV.has_key?(name) && !ENV[name].empty?
				return ENV[name]
			end
		end
		return "/tmp"
	end
	
	def self.process_is_alive?(pid)
		begin
			Process.kill(0, pid)
			return true
		rescue Errno::ESRCH
			return false
		rescue SystemCallError => e
			return true
		end
	end
end # module AdminTools

end # module Passenger
