#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2010-2014 Phusion
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

PhusionPassenger.require_passenger_lib 'debug_logging'
PhusionPassenger.require_passenger_lib 'union_station/core'
PhusionPassenger.require_passenger_lib 'utils/native_support_utils'

module PhusionPassenger
module UnionStation

class Transaction
	include DebugLogging

	attr_reader :txn_id

	def initialize(connection = nil, txn_id = nil)
		if connection
			@connection = connection
			@txn_id = txn_id
			connection.ref
		end
	end

	def null?
		return !@connection || !@connection.connected?
	end

	def message(text)
		if !@connection
			timestamp_string = Core.timestamp_string
			trace(3, "[Union Station log to null] #{@txn_id} #{timestamp_string} #{text}")
			return
		end
		@connection.synchronize do
			return if !@connection.connected?
			begin
				timestamp_string = Core.timestamp_string
				trace(3, "[Union Station log] #{@txn_id} #{timestamp_string} #{text}")
				@connection.channel.write("log", @txn_id, timestamp_string)
				@connection.channel.write_scalar(text)
			rescue SystemCallError, IOError => e
				@connection.disconnect
				warn("Error communicating with the logging agent: #{e.message}")
			rescue Exception => e
				@connection.disconnect
				raise e
			end
		end
	end

	def begin_measure(name, extra_info = nil)
		if extra_info
			extra_info_base64 = [extra_info].pack("m")
			extra_info_base64.gsub!("\n", "")
			extra_info_base64.strip!
		else
			extra_info_base64 = nil
		end
		times = Utils::NativeSupportUtils.process_times
		message "BEGIN: #{name} (#{current_timestamp.to_s(36)},#{times.utime.to_s(36)},#{times.stime.to_s(36)}) #{extra_info_base64}"
	end

	def end_measure(name, error_encountered = false)
		times = Utils::NativeSupportUtils.process_times
		if error_encountered
			message "FAIL: #{name} (#{current_timestamp.to_s(36)},#{times.utime.to_s(36)},#{times.stime.to_s(36)})"
		else
			message "END: #{name} (#{current_timestamp.to_s(36)},#{times.utime.to_s(36)},#{times.stime.to_s(36)})"
		end
	end

	def measure(name, extra_info = nil)
		begin_measure(name, extra_info)
		begin
			yield
		rescue Exception
			error = true
			is_closed = closed?
			raise
		ensure
			end_measure(name, error) if !is_closed
		end
	end

	def measured_time_points(name, begin_time, end_time, extra_info = nil)
		if extra_info
			extra_info_base64 = [extra_info].pack("m")
			extra_info_base64.gsub!("\n", "")
			extra_info_base64.strip!
		else
			extra_info_base64 = nil
		end
		begin_timestamp = begin_time.to_i * 1_000_000 + begin_time.usec
		end_timestamp = end_time.to_i * 1_000_000 + end_time.usec
		message "BEGIN: #{name} (#{begin_timestamp.to_s(36)}) #{extra_info_base64}"
		message "END: #{name} (#{end_timestamp.to_s(36)})"
	end

	def close(flush_to_disk = false)
		@connection.synchronize do
			return if !@connection.connected?
			begin
				# We need an ACK here. See thread_handler.rb finalize_request.
				@connection.channel.write("closeTransaction", @txn_id,
					Core.timestamp_string, true)
				result = @connection.channel.read
				if result != ["ok"]
					raise "Expected logging agent to respond with 'ok', but got #{result.inspect} instead"
				end
				if flush_to_disk
					@connection.channel.write("flush")
					result = @connection.channel.read
					if result != ["ok"]
						raise "Invalid logging agent response #{result.inspect} to the 'flush' command"
					end
				end
			rescue SystemCallError, IOError => e
				@connection.disconnect
				warn("Error communicating with the logging agent: #{e.message}")
			rescue Exception => e
				@connection.disconnect
				raise e
			ensure
				@connection.unref
				@connection = nil
			end
		end if @connection
	end

	def closed?
		if @connection
			@connection.synchronize do
				return !@connection.connected?
			end
		else
			return nil
		end
	end

private
	def current_timestamp
		time = Core.current_time
		return time.to_i * 1_000_000 + time.usec
	end
end

end # module UnionStation
end # module PhusionPassenger
