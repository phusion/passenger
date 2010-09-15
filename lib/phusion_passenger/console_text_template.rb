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

require 'erb'
module PhusionPassenger

class ConsoleTextTemplate
	TEMPLATE_DIR = "#{File.dirname(__FILE__)}/templates"

	def initialize(input, options = {})
		@buffer = ''
		if input[:file]
			data = File.read("#{TEMPLATE_DIR}/#{input[:file]}.txt.erb")
		else
			data = input[:text]
		end
		@template = ERB.new(substitute_color_tags(data),
			nil, nil, '@buffer')
		options.each_pair do |name, value|
			self[name] = value
		end
	end
	
	def []=(name, value)
		instance_variable_set("@#{name}".to_sym, value)
		return self
	end
	
	def result
		return @template.result(binding)
	end

private
	DEFAULT_TERMINAL_COLORS = "\e[0m\e[37m\e[40m"

	def substitute_color_tags(data)
		data = data.gsub(%r{<b>(.*?)</b>}m, "\e[1m\\1#{DEFAULT_TERMINAL_COLORS}")
		data.gsub!(%r{<red>(.*?)</red>}m, "\e[1m\e[31m\\1#{DEFAULT_TERMINAL_COLORS}")
		data.gsub!(%r{<green>(.*?)</green>}m, "\e[1m\e[32m\\1#{DEFAULT_TERMINAL_COLORS}")
		data.gsub!(%r{<yellow>(.*?)</yellow>}m, "\e[1m\e[33m\\1#{DEFAULT_TERMINAL_COLORS}")
		data.gsub!(%r{<banner>(.*?)</banner>}m, "\e[33m\e[44m\e[1m\\1#{DEFAULT_TERMINAL_COLORS}")
		return data
	end
end

end # module PhusionPassenger
