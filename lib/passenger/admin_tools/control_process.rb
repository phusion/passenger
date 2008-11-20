require 'rexml/document'
require 'fileutils'
require 'passenger/admin_tools'
require 'passenger/message_channel'

module Passenger
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
		Dir["/tmp/passenger.*"].each do |dir|
			dir =~ /passenger.(\d+)\Z/
			next if !$1
			pid = $1.to_i
			begin
				results << ControlProcess.new(pid, dir)
			rescue ArgumentError
				# Stale Passenger temp folder. Clean it up if instructed.
				if clean_stale
					puts "*** Cleaning stale folder #{dir}"
					FileUtils.rm_rf(dir)
				end
			end
		end
		return results
	end
	
	def initialize(pid, path = nil)
		if !AdminTools.process_is_alive?(pid)
			raise ArgumentError, "There is no control process with PID #{pid}."
		end
		@pid = pid
		if path
			@path = path
		else
			@path = "/tmp/passenger.#{pid}"
		end
	end
	
	def status
		reload
		return @status
	end
	
	def xml
		reload
		return @xml
	end
	
	def domains
		reload
		return @domains
	end
	
	def instances
		return domains.map do |domain|
			domain[:instances]
		end.flatten
	end
	
private
	def reload
		return if @status
		File.open("#{path}/status.fifo", 'r') do |f|
			channel = MessageChannel.new(f)
			@status = channel.read_scalar
			@xml = channel.read_scalar
		end
		doc = REXML::Document.new(@xml)
		
		@domains = []
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
			@domains << d
		end
	end
end

end # module AdminTools
end # module Passenger
