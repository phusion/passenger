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

class ControlProcess
	class Instance
		attr_accessor :pid, :socket_name, :socket_type, :sessions, :uptime
		INT_PROPERTIES = [:pid, :sessions]
	end

	attr_accessor :path
	attr_accessor :pid
	
	def self.list(clean_stale = true)
		results = []
		Dir["#{AdminTools.tmpdir}/passenger.*"].each do |dir|
			next if dir !~ /passenger.(\d+)\Z/
			begin
				results << ControlProcess.new(dir)
			rescue ArgumentError
				# Stale Passenger temp folder. Clean it up if instructed.
				if clean_stale
					puts "*** Cleaning stale folder #{dir}"
					FileUtils.chmod_R(0700, dir) rescue nil
					FileUtils.rm_rf(dir)
				end
			end
		end
		return results
	end
	
	def self.for_pid(pid)
		return list(false).find { |c| c.pid == pid }
	end
	
	def initialize(path)
		@path = path
		if File.exist?("#{path}/control_process.pid")
			data = File.read("#{path}/control_process.pid").strip
			if data.empty?
				raise ArgumentError, "'#{path}' is not a valid control process directory."
			else
				@pid = data.to_i
			end
		else
			path =~ /passenger.(\d+)\Z/
			@pid = $1.to_i
		end
		if !AdminTools.process_is_alive?(@pid)
			raise ArgumentError, "There is no control process with PID #{@pid}."
		end
	end
	
	def status
		connect do |channel|
			channel.write("status")
			return channel.read_scalar
		end
	end
	
	def backtraces
		connect do |channel|
			channel.write("backtraces")
			return channel.read_scalar
		end
	end
	
	def xml
		connect do |channel|
			channel.write("status_xml")
			return channel.read_scalar
		end
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
	def connect
		channel = MessageChannel.new(UNIXSocket.new("#{path}/master/pool_controller.socket"))
		begin
			yield channel
		ensure
			channel.close
		end
	end
end

end # module AdminTools
end # module PhusionPassenger
