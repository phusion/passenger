require 'thread'

module PhusionPassenger
module Utils

module RobustInterruption
	class Interrupted < StandardError
	end

	class Data
		attr_reader :interruption_flags
		attr_accessor :interrupted
		alias interrupted? interrupted

		def initialize
			@mutex = Mutex.new
			@interruption_flags = [true]
			@interrupted = false
		end

		def lock
			@mutex.lock
		end

		def try_lock
			return @mutex.try_lock
		end

		def unlock
			@mutex.unlock
		end

		def interruptable?
			return @interruption_flags.last
		end

		def push_interruption_flag(value)
			@interruption_flags.push(value)
		end

		def pop_interruption_flag
			raise "BUG - cannot pop interruption flag in this state" if @interruption_flags.size <= 1
			@interruption_flags.pop
		end
	end

	def self.install(thread = Thread.current)
		thread[:robust_interruption] = Data.new
	end

	def install_robust_interruption
		RobustInterruption.install
	end

	def interrupted?(thread = Thread.current)
		data = thread[:robust_interruption]
		return data && data.interrupted?
	end
	module_function :interrupted?

	def self.raise(thread, exception = Interrupted)
		RobustInterruption.disable_interruptions do
			data = thread[:robust_interruption]
			if data
				data.interrupted = true
				if data.try_lock
					begin
						thread.raise(exception)
					ensure
						data.unlock
					end
				end
			else
				thread.raise(exception)
			end
		end
	end

	def disable_interruptions(thread = Thread.current)
		data = thread[:robust_interruption]
		if data
			was_interruptable = data.interruptable?
			data.lock if was_interruptable
			data.push_interruption_flag(false)
			begin
				yield
			ensure
				data.pop_interruption_flag
				data.unlock if was_interruptable
			end
		else
			yield
		end
	end
	module_function :disable_interruptions

	def enable_interruptions(thread = Thread.current)
		data = thread[:robust_interruption]
		if data
			was_interruptable = data.interruptable?
			data.push_interruption_flag(true)
			data.unlock if !was_interruptable
			begin
				yield
			ensure
				data.lock if !was_interruptable
				data.pop_interruption_flag
			end
		else
			yield
		end
	end
	module_function :enable_interruptions

	def restore_interruptions(thread = Thread.current)
		data = thread[:robust_interruption]
		if data
			if data.interruption_flags.size < 2
				Kernel.raise "Cannot restore interruptions state to previous value - no previous value exists"
			end
			if data.interruption_flags[-2]
				enable_interruptions do
					yield
				end
			else
				disable_interruptions do
					yield
				end
			end
		else
			yield
		end
	end
	module_function :restore_interruptions
end

end # module Utils
end # module PhusionPassenger
