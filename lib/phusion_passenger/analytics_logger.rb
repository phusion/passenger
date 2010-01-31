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

require 'digest/md5'
require 'phusion_passenger/message_client'

module PhusionPassenger

class AnalyticsLogger
	class Log
		attr_reader :group_name
		attr_reader :txn_id
		
		def initialize(io = nil, group_name = nil, txn_id = nil, large_messages = false)
			if io
				@io = io
				@group_name = group_name
				@txn_id = txn_id
				@large_messages = large_messages
				message("ATTACH")
			end
		end
		
		def null?
			return !!@io
		end
		
		def message(text)
			if @io
				if @large_messages
					@io.flock(File::LOCK_EX)
					begin
						data = "#{@txn_id} #{Log.timestamp} #{text}\n"
						if data.size > 0xffff
							raise IOError, "Cannot log messages larger than #{0xffff} bytes."
						end
						@io.write(sprintf("%4x ", data.size))
						@io.write(data)
					ensure
						@io.flock(File::LOCK_UN)
					end
				else
					@io.write("#{@txn_id} #{Log.timestamp} #{text}\n")
				end
			end
		end
		
		def close
			if @io
				message("DETACH")
				# Don't close the IO object, it's cached by AnalyticsLogger.
			end
		end
	
	private
		def self.timestamp
			time = Time.now
			return time.to_i * 1000000 + time.usec
		end
	end
	
	class CachedFileHandle < Struct.new(:io, :last_used)
		def close
			io.close
		end
	end
	
	def initialize(logging_agent_address, username, password)
		@server_address = logging_agent_address
		@username = username
		@password = password
		@file_handle_cache = {}
	end
	
	def close
		@file_handle_cache.each_value do |handle|
			handle.close
		end
	end
	
	def continue_transaction(group_name, txn_id, category = :web, large_messages = false)
		if group_name.empty? || txn_id.empty?
			return Log.new
		else
			timestamp = extract_timestamp(txn_id)
			if !timestamp
				raise ArgumentError, "Invalid transaction ID '#{txn_id}'"
			end
			return Log.new(open_log_file(group_name, timestamp, category),
				group_name, txn_id, large_messages)
		end
	end

private
	def extract_timestamp(txn_id)
		timestamp_str = txn_id.split('-', 2)[1]
		if timestamp_str
			return timestamp_str.to_i
		else
			return nil
		end
	end
	
	def open_log_file(group_name, timestamp, category)
		group_id = Digest::MD5.hexdigest(group_name)
		timestamp_sec  = timestamp / 1000000
		timestamp_usec = timestamp % 1000000
		time = Time.at(timestamp_sec, timestamp_usec)
		date_name = time.strftime("%Y/%m/%d/%H")
		log_file_path = "1/#{group_id}/#{category}/#{date_name}/log.txt"
		
		handle = @file_handle_cache[log_file_path]
		if handle
			handle.last_used = Time.now
			return handle.io
		else
			# I think we only need to cache 1 file handle...
			while @file_handle_cache.size > 2
				oldest = nil
				
				@file_handle_cache.each_pair do |log_file_path, handle|
					if !oldest || handle.last_used < @file_handle_cache[oldest].last_used
						oldest = log_file_path
					end
				end
				@file_handle_cache[oldest].close
				@file_handle_cache.delete(oldest)
			end
			
			client = MessageClient.new(@username, @password, @server_address)
			log_file_io = nil
			begin
				client.write("open log file", group_name, timestamp, category)
				result = client.read
				if !result
					raise IOError, "The logging agent unexpectedly closed the connection."
				elsif result[0] == "error"
					raise IOError, "The logging agent could not open the log file: #{result[1]}"
				end
				io = client.recv_io(File)
				io.sync = true
				handle = CachedFileHandle.new(io, Time.now)
				@file_handle_cache[log_file_path] = handle
				return io
			rescue Exception
				log_file_io.close if log_file_io
				raise
			ensure
				client.close
			end
		end
	end
end

end # module PhusionPassenger