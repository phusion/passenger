#  Phusion Passenger - http://www.modrails.com/
#  Copyright (C) 2008  Phusion
#
#  Phusion Passenger is a trademark of Hongli Lai & Ninh Bui.
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; version 2 of the License.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License along
#  with this program; if not, write to the Free Software Foundation, Inc.,
#  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

require 'erb'

module Passenger

# A convenience utility class for rendering our error pages.
class HTMLTemplate
	PASSENGER_FILE_PREFIX = File.dirname(__FILE__)
	TEMPLATE_DIR = "#{PASSENGER_FILE_PREFIX}/templates"

	def initialize(template_name, options = {})
		@buffer = ''
		@template = ERB.new(File.read("#{TEMPLATE_DIR}/#{template_name}.html.erb"),
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
		layout_template = ERB.new(File.read("#{TEMPLATE_DIR}/#{template_name}.html.erb"))
		b = get_binding do
			old_size = @buffer.size
			yield
			@buffer.slice!(old_size .. @buffer.size)
		end
		@buffer << layout_template.result(b)
	end
	
	def include(filename)
		return File.read("#{TEMPLATE_DIR}/#{filename}")
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
			in_passenger ||= starts_with(filename, PASSENGER_FILE_PREFIX)
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

end # module Passenger
