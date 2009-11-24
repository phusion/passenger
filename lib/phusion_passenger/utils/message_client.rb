# encoding: binary
#  Phusion Passenger - http://www.modrails.com/
#  Copyright (c) 2009 Phusion
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

require 'socket'
require 'phusion_passenger/message_channel'
require 'phusion_passenger/utils'

module PhusionPassenger
module Utils

# A convenience class for communicating with MessageServer servers,
# for example the ApplicationPool server.
class MessageClient
	# Connect to the given server. By default it connects to the current
	# generation's helper server.
	def initialize(username, password, filename = "#{Utils.passenger_tmpdir}/socket")
		@socket = UNIXSocket.new(filename)
		@channel = MessageChannel.new(@socket)
		@channel.write_scalar(username)
		@channel.write_scalar(password)
		
		result = @channel.read
		if result.nil?
			raise EOFError
		elsif result[0] != "ok"
			raise SecurityError, result[0]
		end
	end
	
	def close
		@socket.close
	end
	
	### ApplicationPool::Server methods ###
	
	def detach(detach_key)
		write("detach", detach_key)
		check_security_response
		result = read
		if result.nil?
			raise EOFError
		else
			return result.first == "true"
		end
	end
	
	### Low level I/O methods ###
	
	def read
		return @channel.read
	end
	
	def write(*args)
		@channel.write(*args)
	end
	
	def write_scalar(*args)
		@channel.write_scalar(*args)
	end
	
	def check_security_response
		result = @channel.read
		if result.nil?
			raise EOFError
		elsif result[0] != "Passed security"
			raise SecurityError, result[0]
		end
	end
end

end # module Utils
end # module PhusionPassenger