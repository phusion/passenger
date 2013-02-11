#  Phusion Passenger - https://www.phusionpassenger.com/
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
require 'erb'

module PhusionPassenger
module Standalone

class ConfigFile
	class DisallowedContextError < StandardError
		attr_reader :filename
		attr_reader :line
		
		def initialize(message, filename, line)
			super(message)
			@filename = filename
			@line = line
		end
	end
	
	attr_reader :options
	
	def initialize(context, filename)
		@options  = {}
		@context  = context
		@filename = filename
		File.open(filename, 'r') do |f|
			f.flock(File::LOCK_SH)
			instance_eval(f.read, filename)
		end
	end
	
	def address(addr)
		allowed_contexts(:port, :global_config)
		@options[:address] = addr
		@options[:tcp_explicitly_given] = true
	end
	
	def port(number)
		allowed_contexts(:port, :global_config)
		@options[:port] = number
		@options[:tcp_explicitly_given] = true
	end
	
	def environment(name)
		@options[:env] = name
	end
	
	alias env environment
	alias rails_env environment
	alias rack_env environment
	
	def max_pool_size(number)
		allowed_contexts(:max_pool_size, :global_config)
		@options[:max_pool_size] = number
	end
	
	def min_instances(number)
		@options[:min_instances] = number
	end
	
	def daemonize(on)
		allowed_contexts(:daemonize, :global_config)
		@options[:daemonize] = on
	end
	
	def nginx_bin(filename)
		allowed_contexts(:nginx_bin, :global_config)
		@options[:nginx_bin] = filename
	end
	
	def domain_name(name)
		allowed_contexts(:domain_name, :local_config)
		@options[:server_names] = [name]
	end
	
	def domain_names(*names)
		allowed_contexts(:domain_names, :local_config)
		@options[:server_names] = names.to_a.flatten
	end
	
	def analytics(value)
		@options[:analytics] = value
	end
	
	def debugger(value)
		@options[:debugger] = value
	end

private
	def allowed_contexts(option_name, *contexts)
		if !contexts.include?(@context)
			raise DisallowedContextError.new("The '#{option_name}' option may not be set in this config file.",
				@filename, caller[1].split(':')[1].to_i)
		end
	end
end

end # module Standalone
end # module PhusionPassenger
