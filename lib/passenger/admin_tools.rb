module Passenger

module AdminTools
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
