#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2010-2013 Phusion
#
#  "Phusion Passenger" is a trademark of Hongli Lai & Ninh Bui.
#
#  Permission is hereby granted, free of charge, to any person obtaining a copy
#  of this software and associated documentation files (the "Software"), to deal
#  in the Software without restriction, including without limitation the rights
#  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
#  copies of the Software, and to permit persons to whom the Software is
#  furnished to do so, subject to the following conditions:
#
#  The above copyright notice and this permission notice shall be included in
#  all copies or substantial portions of the Software.
#
#  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
#  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
#  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
#  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
#  THE SOFTWARE.

module PhusionPassenger

module DebugLogging
	# We don't refer to STDERR directly because STDERR's reference might
	# change during runtime.
	@@log_level = 0
	@@log_device = nil
	@@log_filename = nil
	@@stderr_evaluator = lambda { STDERR }
	
	def self.included(klass)
		klass.class_eval do
			private :debug
			private :trace
		end
	end

	def self.log_level
		return @@log_level
	end
	
	def self.log_level=(level)
		@@log_level = level
	end
	
	def self.log_file=(filename)
		if filename && filename.empty?
			@@log_filename = nil
		else
			@@log_filename = filename
		end
		@@log_device.close if @@log_device && !@@log_device.closed?
		@@log_device = nil
	end
	
	def self._log_device
		return @@log_device
	end
	
	def self.stderr_evaluator=(block)
		if block
			@@stderr_evaluator = block
		else
			@@stderr_evaluator = lambda { STDERR }
		end
	end
	
	def error(message)
		trace(-1, message, 1)
	end
	module_function :error
	
	def warn(message)
		trace(0, message, 1)
	end
	module_function :warn
	
	def debug(message)
		trace(1, message, 1)
	end
	module_function :debug
	
	def trace(level, message, nesting_level = 0)
		if @@log_level >= level
			if @@log_filename
				if !@@log_device || @@log_device.closed?
					@@log_device = File.open(@@log_filename, "a")
				end
				output = @@log_device
			else
				output = @@stderr_evaluator.call
			end
			location = caller[nesting_level].sub(/.*phusion_passenger\//, '')
			location.sub!(/(.*):.*/, '\1')
			now = Time.now
			time_str = now.strftime("%Y-%m-%d %H:%M:%S.")
			time_str << sprintf("%04d", now.usec / 100)

			current_thread = Thread.current
			if !(thread_id = current_thread[:id])
				current_thread.to_s =~ /:(0x[0-9a-f]+)/i
				thread_id = current_thread[:id] = $1 || '?'
			end
			if thread_name = current_thread[:name]
				thread_name = "(#{thread_name})"
			end

			output.write("[ #{time_str} #{$$}/#{thread_id}#{thread_name} #{location} ]: #{message}\n")
			output.flush
		end
	end
	module_function :trace
end

end
