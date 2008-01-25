require 'rubygems'
require 'thread'
require 'fastthread'
require 'timeout'

class ConditionVariable
	# This is like ConditionVariable.wait(), but allows one to wait a maximum
	# amount of time. Returns true if this condition was signaled, false if a
	# timeout occurred.
	def timed_wait(mutex, secs)
		if secs > 0
			Timeout.timeout(secs) do
				wait(mutex)
			end
		else
			wait(mutex)
		end
		return true
	rescue Timeout::Error
		return false
	end
	
	def timed_wait!(mutex, secs)
		if secs > 0
			Timeout.timeout(secs) do
				wait(mutex)
			end
		else
			wait(mutex)
		end
	end
end

module GC
	if !respond_to?(:cow_friendly?)
		def self.cow_friendly?
			return false
		end
	end
end