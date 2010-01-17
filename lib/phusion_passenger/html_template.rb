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
require 'phusion_passenger'

module PhusionPassenger

# A convenience utility class for rendering our error pages.
class HTMLTemplate
	def initialize(template_name, options = {})
		@buffer = ''
		@template = ERB.new(File.read("#{TEMPLATES_DIR}/#{template_name}.html.erb"),
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
	include ERB::Util
	
	def get_binding
		return binding
	end
	
	def layout(template_name, options = {})
		options.each_pair do |name, value|
			self[name] = value
		end
		layout_template = ERB.new(File.read("#{TEMPLATES_DIR}/#{template_name}.html.erb"))
		b = get_binding do
			old_size = @buffer.size
			yield
			@buffer.slice!(old_size .. @buffer.size)
		end
		@buffer << layout_template.result(b)
	end
	
	def include(filename)
		return File.read("#{TEMPLATES_DIR}/#{filename}")
	end
	
	def backtrace_html_for(error)
		html = %Q{
			<table class="backtrace">
			<tr class="headers">
				<th>#</th>
				<th>File</th>
				<th>Line</th>
				<th>Location</th>
			</tr>
		}
		in_passenger = false
		error.backtrace.each_with_index do |item, i|
			filename, line, location = item.split(':', 3)
			in_passenger ||= starts_with(filename, "#{LIBDIR}/phusion_passenger")
			class_names = in_passenger ? "passenger" : "framework"
			class_names << ((i & 1 == 0) ? " uneven" : " even")
			html << %Q{
				<tr class="backtrace_line #{class_names}">
					<td class="index">#{i}</td>
					<td class="filename">#{filename}</td>
					<td class="line">#{line}</td>
					<td class="location">#{location}</td>
				</tr>
			}
		end
		html << "</table>\n"
		return html
	end
	
	def starts_with(str, substr)
		return str[0 .. substr.size - 1] == substr
	end
end

end # module PhusionPassenger
