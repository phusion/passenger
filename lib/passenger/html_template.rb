require 'erb'

module Passenger

class HTMLTemplate # :nodoc:
	def initialize(template_name)
		@template = ERB.new(File.read("#{File.dirname(__FILE__)}/templates/#{template_name}.html.erb"))
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
	PASSENGER_FILE_PREFIX = File.dirname(__FILE__)
	
	def backtrace_html_for(error)
		html = %Q{
			<table>
			<tr>
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
			class_name = in_passenger ? "passenger" : "framework"
			html << %Q{
				<tr class="#{class_name}">
					<td>#{i}</td>
					<td>#{filename}</td>
					<td>#{line}</td>
					<td>#{location}</td>
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
