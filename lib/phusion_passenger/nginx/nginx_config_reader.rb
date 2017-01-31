#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2013 Phusion
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

# Reads Nginx configuration files and turns it into easy to query
# Ruby data structures. Processes 'include' directives. Uses
# NginxConfigFileParser internally.
class NginxConfigReader
	class Error < StandardError
	end

	class ItemList < Array
		def initialize(items = nil)
			super()
			concat(items) if items
		end

		def items
			return self
		end

		def block(name)
			check_index
			@blocks.each do |block|
				if block[1] == name
					return block
				end
			end
			return nil
		end

		def find_blocks(name)
			check_index
			results = []
			@blocks.each do |block|
				if block[1] == name
					results << block
				end
			end
			return results
		end

		def directives
			check_index
			return @directives.values
		end

		def directive(name)
			check_index
			return @directives[name]
		end

		def [](name_or_index)
			if name_or_index.is_a?(Integer)
				super(name_or_index)
			elsif d = directive(name_or_index)
				return d.value
			else
				return nil
			end
		end

		def []=(name_or_index, value)
			if name_or_index.is_a?(Integer)
				super(name_or_index, value)
			elsif d = directive(name_or_index)
				d.value = value
			else
				if value.is_a?(Directive)
					d = value
				else
					d = Directive.new([:directive, name_or_index, value])
				end
				@directives[name_or_index] = d
				self << d
			end
		end

		def build_index
			@directives = {}
			@blocks = []
			each do |item|
				case item[0]
				when :directive
					@directives[item.name] = item
				when :block
					@blocks << item
				end
			end
		end

	private
		def check_index
			raise "No index built" if !@directives || !@blocks
		end
	end

	class Block < Array
		def initialize(args)
			super()
			concat(args)
		end

		def items
			return last
		end

		def block(name)
			return items.block(name)
		end

		def find_blocks(name)
			return items.find_blocks(name)
		end

		def directives
			return items.directives
		end

		def directive(name)
			return items.directive(name)
		end

		def [](name_or_index)
			if name_or_index.is_a?(Integer)
				super(name_or_index)
			else
				return items[name_or_index]
			end
		end

		def []=(name_or_index, value)
			if name_or_index.is_a?(Integer)
				super(name_or_index, value)
			else
				items[name_or_index] = value
			end
		end

		def build_index
			items.build_index
		end
	end

	class Directive < Array
		def initialize(args)
			super()
			concat(args)
		end

		def scalar?
			return size == 3
		end

		def name
			return self[1]
		end

		def name=(value)
			self[1] = value
		end

		def value
			return self[2]
		end

		def values
			return self[2 .. -1]
		end
	end

	def initialize(filename, *options)
		@root_filename = filename
		@parser_options = options
	end

	def start
		return parse_and_process_data(@root_filename)
	end

private
	def parse_and_process_data(filename)
		content = nil
		begin
			File.open(filename, "rb") do |f|
				content = f.read
			end
		rescue SystemCallError => e
			return [:load_error, e]
		end

		parser = NginxConfigFileParser.new
		result = parser.parse(content, *@parser_options)
		if result
			return [:ok, process_data(result.to_data)]
		else
			return [:parse_error, parser]
		end
	end

	def include_glob(glob, output)
		glob = File.expand_path(glob, File.dirname(@root_filename))
		Dir[glob].each do |filename|
			include_file(filename, output)
		end
	end

	def include_file(filename, output)
		filename = File.expand_path(filename, File.dirname(@root_filename))
		result, aux = parse_and_process_data(filename)
		case result
		when :ok
			output.concat(aux)
		when :parse_error
			raise Error, "Cannot parse included file #{filename.inspect}: #{aux.failure_reason}"
		when :load_error
			raise Error, "Cannot load included file #{filename.inspect}: #{aux}"
		else
			raise "Bug"
		end
	end

	def process_data(data)
		result = ItemList.new
		data.each do |element|
			case element[0]
			when :directive
				if element[1] == 'include'
					if element.size != 3
						raise "'include' directive may only have 1 parameter"
					elsif element[2] =~ /\*\?\[/ # Nginx performs the same check
						include_glob(element[2], result)
					else
						include_file(element[2], result)
					end
				else
					result << Directive.new(element)
				end
			when :block
				block = Block.new(element[0..-2] + [process_data(element.last)])
				block.build_index
				result << block
			else
				result << element
			end
		end
		result.build_index
		return result
	end
end
