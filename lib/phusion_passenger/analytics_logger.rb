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
require 'phusion_passenger/message_client'

module PhusionPassenger

class AnalyticsLogger
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
			return !!@shared_data
		end
		
		def message(text)
			@shared_data.synchronize do
				@shared_data.client.write("log", @txn_id,
					AnalyticsLogger.timestamp_string)
				@shared_data.client.write_scalar(text)
			end if @shared_data
		end
		
		def begin_measure(name)
			times = NativeSupport.process_times
			message "BEGIN: #{name} (utime = #{times.utime}, stime = #{times.stime})"
		end
		
		def end_measure(name, error_encountered = false)
			times = NativeSupport.process_times
			if error_encountered
				message "FAIL: #{name} (utime = #{times.utime}, stime = #{times.stime})"
			else
				message "END: #{name} (utime = #{times.utime}, stime = #{times.stime})"
			end
		end
		
		def measure(name)
			begin_measure(name)
			begin
				yield
			rescue Exception
				error = true
				raise
			ensure
				end_measure(name, error)
			end
		end
		
		def close
			@shared_data.synchronize do
				@shared_data.client.write("closeTransaction", @txn_id,
					AnalyticsLogger.timestamp_string)
				@shared_data.unref
				@shared_data = nil
			end if @shared_data
		end
	end
	
	def self.new_from_options(options)
		if options["logging_agent_address"]
			return new(options["logging_agent_address"],
				options["logging_agent_username"],
				options["logging_agent_password_base64"].unpack('m').first,
				options["node_name"])
		else
			return nil
		end
	end
	
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
		@shared_data = SharedData.new
	end
	
	def close
		@shared_data.synchronize do
			@shared_data.unref
			@shared_data = nil
		end
		@random_dev.close
	end
	
	def new_transaction(group_name, category = :requests)
		if !@server_address || !group_name
			return Log.new
		else
			txn_id = (AnalyticsLogger.current_time.to_i / 60).to_s(16)
			txn_id << "-#{random_token(11)}"
			@shared_data.synchronize do
				connect if !connected?
				begin
					@shared_data.client.write("openTransaction",
						txn_id, group_name, category,
						AnalyticsLogger.timestamp_string)
					return Log.new(@shared_data, txn_id)
				rescue
					disconnect
					raise
				end
			end
		end
	end
	
	def continue_transaction(txn_id, group_name, category = :requests)
		if !@server_address || !txn_id
			return Log.new
		else
			@shared_data.synchronize do
				connect if !connected?
				begin
					@shared_data.client.write("openTransaction",
						txn_id, group_name, category,
						AnalyticsLogger.timestamp_string)
					return Log.new(@shared_data, txn_id)
				rescue
					disconnect
					raise
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
	
	class SharedData
		attr_accessor :client
		
		def initialize
			@mutex = Mutex.new
			@refcount = 1
		end
		
		def ref
			@refcount += 1
		end
		
		def unref
			@refcount -= 1
			if @refcount == 0
				@client.close if @client
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
	end
	
	def disconnect
		@shared_data.unref
		@shared_data = SharedData.new
	end
	
	def random_token(length)
		token = ""
		@random_dev.read(length).each_char do |c|
			token << RANDOM_CHARS[c[0].ord % RANDOM_CHARS.size]
		end
		return token
	end
	
	def self.current_time
		return Time.now
	end
	
	def self.timestamp_string(time = current_time)
		timestamp = time.to_i * 1_000_000 + time.usec
		return timestamp.to_s(16)
	end
end

end # module PhusionPassenger