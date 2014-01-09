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

require 'thread'
PhusionPassenger.require_passenger_lib 'utils'
PhusionPassenger.require_passenger_lib 'utils/native_support_utils'
PhusionPassenger.require_passenger_lib 'debug_logging'
PhusionPassenger.require_passenger_lib 'message_channel'

module PhusionPassenger

class AnalyticsLogger
	RETRY_SLEEP = 0.2
	NETWORK_ERRORS = [Errno::EPIPE, Errno::ECONNREFUSED, Errno::ECONNRESET,
		Errno::EHOSTUNREACH, Errno::ENETDOWN, Errno::ENETUNREACH, Errno::ETIMEDOUT]
	
	include Utils
	
	class Log
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
				timestamp_string = AnalyticsLogger.timestamp_string
				DebugLogging.trace(3, "[Union Station log to null] #{@txn_id} #{timestamp_string} #{text}")
				return
			end
			@connection.synchronize do
				return if !@connection.connected?
				begin
					timestamp_string = AnalyticsLogger.timestamp_string
					DebugLogging.trace(3, "[Union Station log] #{@txn_id} #{timestamp_string} #{text}")
					@connection.channel.write("log", @txn_id, timestamp_string)
					@connection.channel.write_scalar(text)
				rescue SystemCallError, IOError => e
					@connection.disconnect
					DebugLogging.warn("Error communicating with the logging agent: #{e.message}")
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
						AnalyticsLogger.timestamp_string, true)
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
					DebugLogging.warn("Error communicating with the logging agent: #{e.message}")
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
			time = AnalyticsLogger.current_time
			return time.to_i * 1_000_000 + time.usec
		end
	end
	
	def self.new_from_options(options)
		if options["analytics"] && options["logging_agent_address"]
			return new(options["logging_agent_address"],
				options["logging_agent_username"],
				options["logging_agent_password"],
				options["node_name"])
		else
			return nil
		end
	end
	
	attr_accessor :max_connect_tries
	attr_accessor :reconnect_timeout
	
	def initialize(logging_agent_address, username, password, node_name)
		@server_address = logging_agent_address
		@username = username
		@password = password
		if node_name && !node_name.empty?
			@node_name = node_name
		else
			@node_name = `hostname`.strip
		end
		@random_dev = File.open("/dev/urandom")
		
		# This mutex protects the following instance variables, but
		# not the contents of @connection.
		@mutex = Mutex.new
		
		@connection = Connection.new(nil)
		if @server_address && local_socket_address?(@server_address)
			@max_connect_tries = 10
		else
			@max_connect_tries = 1
		end
		@reconnect_timeout = 1
		@next_reconnect_time = Time.utc(1980, 1, 1)
	end
	
	def clear_connection
		@mutex.synchronize do
			@connection.synchronize do
				@random_dev = File.open("/dev/urandom") if @random_dev.closed?
				@connection.unref
				@connection = Connection.new(nil)
			end
		end
	end
	
	def close
		@mutex.synchronize do
			@connection.synchronize do
				@random_dev.close
				@connection.unref
				@connection = nil
			end
		end
	end
	
	def new_transaction(group_name, category = :requests, union_station_key = "-")
		if !@server_address
			return Log.new
		elsif !group_name || group_name.empty?
			raise ArgumentError, "Group name may not be empty"
		end
		
		txn_id = (AnalyticsLogger.current_time.to_i / 60).to_s(36)
		txn_id << "-#{random_token(11)}"
		
		Lock.new(@mutex).synchronize do |lock|
			if current_time < @next_reconnect_time
				return Log.new
			end
			
			Lock.new(@connection.mutex).synchronize do |connection_lock|
				if !@connection.connected?
					begin
						connect
						connection_lock.reset(@connection.mutex)
					rescue SystemCallError, IOError
						@connection.disconnect
						DebugLogging.warn("Cannot connect to the logging agent at #{@server_address}; " +
							"retrying in #{@reconnect_timeout} second(s).")
						@next_reconnect_time = current_time + @reconnect_timeout
						return Log.new
					rescue Exception => e
						@connection.disconnect
						raise e
					end
				end
				
				begin
					@connection.channel.write("openTransaction",
						txn_id, group_name, "", category,
						AnalyticsLogger.timestamp_string,
						union_station_key,
						true,
						true)
					result = @connection.channel.read
					if result != ["ok"]
						raise "Expected logging server to respond with 'ok', but got #{result.inspect} instead"
					end
					return Log.new(@connection, txn_id)
				rescue SystemCallError, IOError
					@connection.disconnect
					DebugLogging.warn("The logging agent at #{@server_address}" <<
						" closed the connection; will reconnect in " <<
						"#{@reconnect_timeout} second(s).")
					@next_reconnect_time = current_time + @reconnect_timeout
					return Log.new
				rescue Exception => e
					@connection.disconnect
					raise e
				end
			end
		end
	end
	
	def continue_transaction(txn_id, group_name, category = :requests, union_station_key = "-")
		if !@server_address
			return Log.new
		elsif !txn_id || txn_id.empty?
			raise ArgumentError, "Transaction ID may not be empty"
		end
		
		Lock.new(@mutex).synchronize do |lock|
			if current_time < @next_reconnect_time
				return Log.new
			end
			
			Lock.new(@connection.mutex).synchronize do |connection_lock|
				if !@connection.connected?
					begin
						connect
						connection_lock.reset(@connection.mutex)
					rescue SystemCallError, IOError
						@connection.disconnect
						DebugLogging.warn("Cannot connect to the logging agent at #{@server_address}; " +
							"retrying in #{@reconnect_timeout} second(s).")
						@next_reconnect_time = current_time + @reconnect_timeout
						return Log.new
					rescue Exception => e
						@connection.disconnect
						raise e
					end
				end
				
				begin
					@connection.channel.write("openTransaction",
						txn_id, group_name, "", category,
						AnalyticsLogger.timestamp_string,
						union_station_key,
						true)
					return Log.new(@connection, txn_id)
				rescue SystemCallError, IOError
					@connection.disconnect
					DebugLogging.warn("The logging agent at #{@server_address}" <<
						" closed the connection; will reconnect in " <<
						"#{@reconnect_timeout} second(s).")
					@next_reconnect_time = current_time + @reconnect_timeout
					return Log.new
				rescue Exception => e
					@connection.disconnect
					raise e
				end
			end
		end
	end

private
	RANDOM_CHARS = ['a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
		'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
		'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
		'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
		'0', '1', '2', '3', '4', '5', '6', '7', '8', '9']
	
	class Lock
		def initialize(mutex)
			@mutex = mutex
			@locked = false
		end
		
		def reset(mutex, lock_now = true)
			unlock if @locked
			@mutex = mutex
			lock if lock_now
		end
		
		def synchronize
			lock if !@locked
			begin
				yield(self)
			ensure
				unlock if @locked
			end
		end
		
		def lock
			raise if @locked
			@mutex.lock
			@locked = true
		end
		
		def unlock
			raise if !@locked
			@mutex.unlock
			@locked = false
		end
	end
	
	class Connection
		attr_reader :mutex
		attr_accessor :channel
		
		def initialize(io)
			@mutex = Mutex.new
			@refcount = 1
			@channel = MessageChannel.new(io) if io
		end
		
		def connected?
			return !!@channel
		end
		
		def disconnect
			@channel.close if @channel
			@channel = nil
		end
		
		def ref
			@refcount += 1
		end
		
		def unref
			@refcount -= 1
			if @refcount == 0
				disconnect
			end
		end
		
		def synchronize
			@mutex.synchronize do
				yield
			end
		end
	end
	
	def connect
		socket  = connect_to_server(@server_address)
		channel = MessageChannel.new(socket)
		
		result = channel.read
		if result.nil?
			raise EOFError
		elsif result.size != 2 || result[0] != "version"
			raise IOError, "The logging agent didn't sent a valid version identifier"
		elsif result[1] != "1"
			raise IOError, "Unsupported logging agent protocol version #{result[1]}"
		end
		
		channel.write_scalar(@username)
		channel.write_scalar(@password)
		
		result = channel.read
		if result.nil?
			raise EOFError
		elsif result[0] != "ok"
			raise SecurityError, result[0]
		end
		
		channel.write("init", @node_name)
		args = channel.read
		if !args
			raise Errno::ECONNREFUSED, "Cannot connect to logging agent"
		elsif args.size != 1
			raise IOError, "Logging agent returned an invalid reply for the 'init' command"
		elsif args[0] == "server shutting down"
			raise Errno::ECONNREFUSED, "Cannot connect to logging agent"
		elsif args[0] != "ok"
			raise IOError, "Logging agent returned an invalid reply for the 'init' command"
		end
		
		@connection.unref
		@connection = Connection.new(socket)
	rescue Exception => e
		socket.close if socket && !socket.closed?
		raise e
	end
	
	def random_token(length)
		token = ""
		@random_dev.read(length).each_byte do |c|
			token << RANDOM_CHARS[c % RANDOM_CHARS.size]
		end
		return token
	end
	
	def current_time
		return self.class.current_time
	end
	
	def self.current_time
		return Time.now
	end
	
	def self.timestamp_string(time = current_time)
		timestamp = time.to_i * 1_000_000 + time.usec
		return timestamp.to_s(36)
	end
end

end # module PhusionPassenger
