#  Phusion Passenger - http://www.modrails.com/
#  Copyright (c) 2008, 2009 Phusion
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

require 'rexml/document'
require 'fileutils'
require 'socket'
require 'phusion_passenger/admin_tools'
require 'phusion_passenger/message_channel'

module PhusionPassenger
module AdminTools

class ServerInstance
	# If you change the structure version then don't forget to change
	# ext/common/ServerInstanceDir.h too.
	
	DIR_STRUCTURE_MAJOR_VERSION = 1
	DIR_STRUCTURE_MINOR_VERSION = 0
	GENERATION_STRUCTURE_MAJOR_VERSION = 1
	GENERATION_STRUCTURE_MINOR_VERSION = 0
	
	STALE_TIME_THRESHOLD = 60
	
	class StaleDirectoryError < StandardError
	end
	class CorruptedDirectoryError < StandardError
	end
	class GenerationsAbsentError < StandardError
	end
	class UnsupportedGenerationStructureVersionError < StandardError
	end
	
	class RoleDeniedError < StandardError
	end
	
	# TODO: really need to do something about the terminology. it should be "backend process" or something.
	class Instance
		attr_accessor :pid, :socket_name, :socket_type, :sessions, :uptime
		INT_PROPERTIES = [:pid, :sessions]
	end

	attr_reader :path
	attr_reader :generation_path
	attr_reader :pid
	
	def self.list(options = {})
		options = {
			:clean_stale_or_corrupted => true
		}.merge(options)
		instances = []
		
		Dir["#{AdminTools.tmpdir}/passenger.*"].each do |dir|
			next if dir !~ /passenger\.#{DIR_STRUCTURE_MAJOR_VERSION}\.(\d+)\.(\d+)\Z/
			minor = $1
			next if minor.to_i > DIR_STRUCTURE_MINOR_VERSION
			
			begin
				instances << ServerInstance.new(dir)
			rescue StaleDirectoryError, CorruptedDirectoryError
				if options[:clean_stale_or_corrupted] &&
				   File.stat(dir).ctime < current_time - STALE_TIME_THRESHOLD
					log_cleaning_action(dir)
					FileUtils.chmod_R(0700, dir) rescue nil
					FileUtils.rm_rf(dir)
				end
			rescue UnsupportedGenerationStructureVersionError, GenerationsAbsentError
				# Do nothing.
			end
		end
		return instances
	end
	
	def self.for_pid(pid, options = {})
		return list(options).find { |c| c.pid == pid }
	end
	
	def initialize(path)
		raise ArgumentError, "Path may not be nil." if path.nil?
		@path = path
		
		if File.exist?("#{path}/control_process.pid")
			data = File.read("#{path}/control_process.pid").strip
			@pid = data.to_i
		else
			path =~ /passenger\.\d+\.\d+\.(\d+)\Z/
			@pid = $1.to_i
		end
		if @pid == 0
			raise CorruptedDirectoryError, "Instance directory contains corrupted control_process.pid file."
		elsif !AdminTools.process_is_alive?(@pid)
			raise StaleDirectoryError, "There is no instance with PID #{@pid}."
		end
		
		generations = Dir["#{path}/generation-*"]
		if generations.empty?
			raise GenerationsAbsentError, "There are no generation subdirectories in this instance directory."
		end
		highest_generation_number = 0
		generations.each do |generation|
			generation =~ /(\d+)/
			generation_number = $1.to_i
			if generation_number > highest_generation_number
				highest_generation_number = generation_number
			end
		end
		@generation_path = "#{path}/generation-#{highest_generation_number}"
		
		if !File.exist?("#{@generation_path}/structure_version.txt")
			raise CorruptedDirectoryError, "The generation directory doesn't contain a structure version specification file."
		end
		version_data = File.read("#{@generation_path}/structure_version.txt").strip
		major, minor = version_data.split(".", 2)
		if major.nil? || minor.nil? || major !~ /\A\d+\Z/ || minor !~ /\A\d+\Z/
			raise CorruptedDirectoryError, "The generation directory doesn't contain a valid structure version specification file."
		end
		major = major.to_i
		minor = minor.to_i
		if major != GENERATION_STRUCTURE_MAJOR_VERSION || minor > GENERATION_STRUCTURE_MINOR_VERSION
			raise UnsupportedGenerationStructureVersionError, "Unsupported generation directory structure version."
		end
	end
	
	# Raises:
	# - +ArgumentError+
	# - +RoleDeniedError+: The user that the current process is as is not authorized to utilize the given role.
	# - +EOFError+: The server unexpectedly closed the connection during authentication.
	# - +SecurityError+: The server denied our authentication credentials.
	def connect(role_or_username, password = nil)
		@channel = MessageChannel.new(UNIXSocket.new("#{@generation_path}/socket"))
		if role_or_username.is_a?(Symbol)
			case role_or_username
			when :passenger_status
				username = "_passenger-status"
				begin
					password = File.read("#{@generation_path}/passenger-status-password.txt")
				rescue Errno::EACCES
					raise RoleDeniedError
				end
			else
				raise ArgumentError, "Supported role #{role_or_username}"
			end
		else
			username = role_or_username
		end
		
		begin
			@channel.write_scalar(username)
			@channel.write_scalar(password)
			result = @channel.read
			if result.nil?
				raise EOFError
			elsif result[0] != "ok"
				raise SecurityError, result[0]
			end
			yield self
		ensure
			@channel.close
		end
	end
	
	def status
		@channel.write("inspect")
		check_security_response
		return @channel.read_scalar
	end
	
	def backtraces
		@channel.write("backtraces")
		check_security_response
		return @channel.read_scalar
	end
	
	def xml
		@channel.write("toXml", true)
		check_security_response
		return @channel.read_scalar
	end
	
	def domains
		doc = REXML::Document.new(xml)
		
		domains = []
		doc.elements.each("info/domains/domain") do |domain|
			instances = []
			d = {
				:name => domain.elements["name"].text,
				:instances => instances
			}
			domain.elements.each("instances/instance") do |instance|
				i = Instance.new
				instance.elements.each do |element|
					if i.respond_to?("#{element.name}=")
						if Instance::INT_PROPERTIES.include?(element.name.to_sym)
							value = element.text.to_i
						else
							value = element.text
						end
						i.send("#{element.name}=", value)
					end
				end
				instances << i
			end
			domains << d
		end
		return domains
	end
	
	def instances
		return domains.map do |domain|
			domain[:instances]
		end.flatten
	end
	
private
	def self.log_cleaning_action(dir)
		puts "*** Cleaning stale folder #{dir}"
	end
	
	def self.current_time
		Time.now
	end
	
	class << self;
		private :log_cleaning_action
		private :current_time
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

end # module AdminTools
end # module PhusionPassenger
