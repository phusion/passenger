#  Phusion Passenger - http://www.modrails.com/
#  Copyright (c) 2010 Phusion
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
require 'phusion_passenger/utils'
require 'phusion_passenger/debug_logging'
require 'phusion_passenger/message_client'

module PhusionPassenger

class AnalyticsLogger
	RETRY_SLEEP = 0.2
	NETWORK_ERRORS = [Errno::EPIPE, Errno::ECONNREFUSED, Errno::ECONNRESET,
		Errno::EHOSTUNREACH, Errno::ENETDOWN, Errno::ENETUNREACH, Errno::ETIMEDOUT]
	
	include Utils
	
	class Log
		attr_reader :txn_id
		
		def initialize(shared_data = nil, txn_id = nil)
			if shared_data
				@shared_data = shared_data
				@txn_id = txn_id
				shared_data.ref
			end
		end
		
		def null?
			return !@shared_data
		end
		
		def message(text)
			@shared_data.synchronize do
				@shared_data.client.write("log", @txn_id,
					AnalyticsLogger.timestamp_string)
				@shared_data.client.write_scalar(text)
			end if @shared_data
		end
		
		def begin_measure(name, extra_info = nil)
			if extra_info
				extra_info_base64 = [extra_info].pack("m")
				extra_info_base64.gsub!("\n", "")
				extra_info_base64.strip!
			else
				extra_info_base64 = nil
			end
			times = NativeSupport.process_times
			message "BEGIN: #{name} (#{current_timestamp.to_s(36)},#{times.utime.to_s(36)},#{times.stime.to_s(36)}) #{extra_info_base64}"
		end
		
		def end_measure(name, error_encountered = false)
			times = NativeSupport.process_times
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
			@shared_data.synchronize do
				# We need an ACK here. See abstract_request_handler.rb finalize_request.
				@shared_data.client.write("closeTransaction", @txn_id,
					AnalyticsLogger.timestamp_string, true)
				result = @shared_data.client.read
				if result != ["ok"]
					raise "Expected logging server to respond with 'ok', but got #{result.inspect} instead"
				end
				if flush_to_disk
					@shared_data.client.write("flush")
					result = @shared_data.client.read
					if result != ["ok"]
						raise "Invalid logging server response #{result.inspect} to the 'flush' command"
					end
				end
				@shared_data.unref
				@shared_data = nil
			end if @shared_data
		end
		
		def closed?
			if @shared_data
				@shared_data.synchronize do
					return !@shared_data.client.connected?
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
				options["logging_agent_password_base64"].unpack('m').first,
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
		# not the contents of @shared_data.
		@mutex = Mutex.new
		
		@shared_data = SharedData.new
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
			@shared_data.synchronize do
				@random_dev = File.open("/dev/urandom") if @random_dev.closed?
				@shared_data.unref
				@shared_data = SharedData.new
			end
		end
	end
	
	def close
		@mutex.synchronize do
			@shared_data.synchronize do
				@random_dev.close
				@shared_data.unref
				@shared_data = nil
			end
		end
	end
	
	def new_transaction(group_name, category = :requests, union_station_key = nil)
		if !@server_address
			return Log.new
		elsif !group_name || group_name.empty?
			raise ArgumentError, "Group name may not be empty"
		end
		
		txn_id = (AnalyticsLogger.current_time.to_i / 60).to_s(36)
		txn_id << "-#{random_token(11)}"
		Lock.new(@mutex).synchronize do |lock|
		Lock.new(@shared_data.mutex).synchronize do |shared_data_lock|
			try_count = 0
			if current_time >= @next_reconnect_time
				while try_count < @max_connect_tries
					begin
						connect if !connected?
						@shared_data.client.write("openTransaction",
							txn_id, group_name, "", category,
							AnalyticsLogger.timestamp_string,
							union_station_key,
							true,
							true)
						result = @shared_data.client.read
						if result != ["ok"]
							raise "Expected logging server to respond with 'ok', but got #{result.inspect} instead"
						end
						return Log.new(@shared_data, txn_id)
					rescue Errno::ENOENT, *NETWORK_ERRORS
						try_count += 1
						disconnect(true)
						shared_data_lock.reset(@shared_data.mutex, false)
						lock.unlock
						sleep RETRY_SLEEP if try_count < @max_connect_tries
						lock.lock
						shared_data_lock.lock
					rescue Exception => e
						disconnect
						raise e
					end
				end
				# Failed to connect.
				DebugLogging.warn("Cannot connect to the logging agent (#{@server_address}); " +
					"retrying in #{@reconnect_timeout} second(s).")
				@next_reconnect_time = current_time + @reconnect_timeout
			end
			return Log.new
		end
		end
	end
	
	def continue_transaction(txn_id, group_name, category = :requests, union_station_key = nil)
		if !@server_address
			return Log.new
		elsif !txn_id || txn_id.empty?
			raise ArgumentError, "Transaction ID may not be empty"
		end
		
		Lock.new(@mutex).synchronize do |lock|
		Lock.new(@shared_data.mutex).synchronize do |shared_data_lock|
			try_count = 0
			if current_time >= @next_reconnect_time
				while try_count < @max_connect_tries
					begin
						connect if !connected?
						@shared_data.client.write("openTransaction",
							txn_id, group_name, "", category,
							AnalyticsLogger.timestamp_string,
							union_station_key,
							true)
						return Log.new(@shared_data, txn_id)
					rescue Errno::ENOENT, *NETWORK_ERRORS
						try_count += 1
						disconnect(true)
						shared_data_lock.reset(@shared_data.mutex, false)
						lock.unlock
						sleep RETRY_SLEEP if try_count < @max_connect_tries
						lock.lock
						shared_data_lock.lock
					rescue Exception => e
						disconnect
						raise e
					end
				end
				# Failed to connect.
				DebugLogging.warn("Cannot connect to the logging agent (#{@server_address}); " +
					"retrying in #{@reconnect_timeout} second(s).")
				@next_reconnect_time = current_time + @reconnect_timeout
			end
			return Log.new
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
	
	class SharedData
		attr_reader :mutex
		attr_accessor :client
		
		def initialize
			@mutex = Mutex.new
			@refcount = 1
		end
		
		def disconnect(check_error_response = false)
			# TODO: implement check_error_response support
			@client.close if @client
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
	
	def connected?
		return @shared_data.client && @shared_data.client.connected?
	end
	
	def connect
		@shared_data.client = MessageClient.new(@username, @password, @server_address)
		@shared_data.client.write("init", @node_name)
		args = @shared_data.client.read
		if !args
			raise Errno::ECONNREFUSED, "Cannot connect to logging server"
		elsif args.size != 1
			raise IOError, "Logging server returned an invalid reply for the 'init' command"
		elsif args[0] == "server shutting down"
			raise Errno::ECONNREFUSED, "Cannot connect to logging server"
		elsif args[0] != "ok"
			raise IOError, "Logging server returned an invalid reply for the 'init' command"
		end
	end
	
	def disconnect(check_error_response = false)
		@shared_data.disconnect(check_error_response)
		@shared_data.unref
		@shared_data = SharedData.new
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