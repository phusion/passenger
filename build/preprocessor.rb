# encoding: utf-8
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

# Implements a simple preprocessor language which combines elements in the C
# preprocessor with ERB:
#
#     Today
#     #if @today == :fine
#         is a fine day.
#     #elif @today == :good
#         is a good day.
#     #else
#         is a sad day.
#     #endif
#     Let's go walking.
#     Today is <%= Time.now %>.
#
# When run with...
#
#     Preprocessor.new.start('input.txt', 'output.txt', :today => :fine)
#
# ...will produce:
#
#     Today
#     is a fine day.
#     Let's go walking.
#     Today is 2013-08-11 22:37:06 +0200.
#
# Highlights:
#
#  * #if blocks can be nested.
#  * Expressions are Ruby expressions, evaluated within the binding of a
#    Preprocessor::Evaluator object.
#  * Text inside #if/#elif/#else are automatically unindented.
#  * ERB compatible.
class Preprocessor
	def initialize
		require 'erb' if !defined?(ERB)
		@indentation_size = 4
		@debug = boolean_option('DEBUG')
	end

	def start(filename, output_filename, variables = {})
		if output_filename
			temp_output_filename = "#{output_filename}._new"
			output = File.open(temp_output_filename, 'w')
		else
			output = STDOUT
		end
		the_binding  = create_binding(variables)
		context      = []
		@filename    = filename
		@lineno      = 1
		@indentation = 0

		each_line(filename, the_binding) do |line|
			debug("context=#{context.inspect}, line=#{line.inspect}")

			name, args_string, cmd_indentation = recognize_command(line)
			case name
			when "if"
				case context.last
				when nil, :if_true, :else_true
					check_indentation(cmd_indentation)
					result = the_binding.eval(args_string, filename, @lineno)
					context.push(result ? :if_true : :if_false)
					inc_indentation
				when :if_false, :else_false, :if_ignore
					check_indentation(cmd_indentation)
					inc_indentation
					context.push(:if_ignore)
				else
					terminate "#if is not allowed in this context"
				end
			when "elif"
				case context.last
				when :if_true
					dec_indentation
					check_indentation(cmd_indentation)
					inc_indentation
					context[-1] = :if_false
				when :if_false
					dec_indentation
					check_indentation(cmd_indentation)
					inc_indentation
					result = the_binding.eval(args_string, filename, @lineno)
					context[-1] = result ? :if_true : :if_false
				when :else_true, :else_false
					terminate "#elif is not allowed after #else"
				when :if_ignore
					dec_indentation
					check_indentation(cmd_indentation)
					inc_indentation
				else
					terminate "#elif is not allowed outside #if block"
				end
			when "else"
				case context.last
				when :if_true
					dec_indentation
					check_indentation(cmd_indentation)
					inc_indentation
					context[-1] = :else_false
				when :if_false
					dec_indentation
					check_indentation(cmd_indentation)
					inc_indentation
					context[-1] = :else_true
				when :else_true, :else_false
					terminate "it is not allowed to have multiple #else clauses in one #if block"
				when :if_ignore
					dec_indentation
					check_indentation(cmd_indentation)
					inc_indentation
				else
					terminate "#else is not allowed outside #if block"
				end
			when "endif"
				case context.last
				when :if_true, :if_false, :else_true, :else_false, :if_ignore
					dec_indentation
					check_indentation(cmd_indentation)
					context.pop
				else
					terminate "#endif is not allowed outside #if block"
				end
			when "DEBHELPER"
				output.puts(line)
			when "", nil
				# Either a comment or not a preprocessor command.
				case context.last
				when nil, :if_true, :else_true
					output.puts(unindent(line))
				else
					# Check indentation but do not output.
					unindent(line)
				end
			else
				terminate "Unrecognized preprocessor command ##{name.inspect}"
			end

			@lineno += 1
		end
	ensure
		if output_filename && output
			output.close
			stat = File.stat(filename)
			File.chmod(stat.mode, temp_output_filename)
			File.chown(stat.uid, stat.gid, temp_output_filename) rescue nil
			File.rename(temp_output_filename, output_filename)
		end
	end

private
	UBUNTU_DISTRIBUTIONS = {
		"lucid"    => "10.04",
		"maverick" => "10.10",
		"natty"    => "11.04",
		"oneiric"  => "11.10",
		"precise"  => "12.04",
		"quantal"  => "12.10",
		"raring"   => "13.04",
		"saucy"    => "13.10",
		"trusty"   => "14.04"
	}
	DEBIAN_DISTRIBUTIONS = {
		"squeeze"  => "20110206",
		"wheezy"   => "20130504",
		"jessie"   => "20140000"
	}
	REDHAT_ENTERPRISE_DISTRIBUTIONS = {
		"el6"      => "el6.0"
	}
	AMAZON_DISTRIBUTIONS = {
		"amazon"   => "amazon"
	}

	# Provides the DSL that's accessible within.
	class Evaluator
		def _infer_distro_table(name)
			if UBUNTU_DISTRIBUTIONS.has_key?(name)
				return UBUNTU_DISTRIBUTIONS
			elsif DEBIAN_DISTRIBUTIONS.has_key?(name)
				return DEBIAN_DISTRIBUTIONS
			elsif REDHAT_ENTERPRISE_DISTRIBUTIONS.has_key?(name)
				return REDHAT_ENTERPRISE_DISTRIBUTIONS
			elsif AMAZON_DISTRIBUTIONS.has_key?(name)
				return AMAZON_DISTRIBUTIONS
			end
		end

		def is_distribution?(expr)
			if @distribution.nil?
				raise "The :distribution variable must be set"
			else
				if expr =~ /^(>=|>|<=|<|==|\!=)[\s]*(.+)/
					comparator = $1
					name = $2
				else
					raise "Invalid expression #{expr.inspect}"
				end

				table1 = _infer_distro_table(@distribution)
				table2 = _infer_distro_table(name)
				raise "Distribution name #{@distribution.inspect} not recognized" if !table1
				raise "Distribution name #{name.inspect} not recognized" if !table2
				return false if table1 != table2
				v1 = table1[@distribution]
				v2 = table2[name]

				case comparator
				when ">"
					return v1 > v2
				when ">="
					return v1 >= v2
				when "<"
					return v1 < v2
				when "<="
					return v1 <= v2
				when "=="
					return v1 == v2
				when "!="
					return v1 != v2
				else
					raise "BUG"
				end
			end
		end
	end

	def each_line(filename, the_binding)
		data = File.open(filename, 'r') do |f|
			erb = ERB.new(f.read, nil, "-")
			erb.filename = filename
			erb.result(the_binding)
		end
		data.each_line do |line|
			yield line.chomp
		end
	end

	def recognize_command(line)
		if line =~ /^([\s\t]*)#(.+)/
			indentation_str = $1
			command = $2

			# Declare tabs as equivalent to 4 spaces. This is necessary for
			# Makefiles in which the use of tabs is required.
			indentation_str.gsub!("\t", "    ")

			name = command.scan(/^\w+/).first
			# Ignore shebangs and comments.
			return if name.nil?

			args_string = command.sub(/^#{Regexp.escape(name)}[\s\t]*/, '')
			return [name, args_string, indentation_str.to_s.size]
		else
			return nil
		end
	end

	def create_binding(variables)
		object = Evaluator.new
		variables.each_pair do |key, val|
			object.send(:instance_variable_set, "@#{key}", val)
		end
		return object.instance_eval do
			binding
		end
	end

	def inc_indentation
		@indentation += @indentation_size
	end

	def dec_indentation
		@indentation -= @indentation_size
	end

	def check_indentation(expected)
		if expected != @indentation
			terminate "wrong indentation: found #{expected} characters, should be #{@indentation}"
		end
	end

	def unindent(line)
		line =~ /^([\s\t]*)/
		# Declare tabs as equivalent to 4 spaces. This is necessary for
		# Makefiles in which the use of tabs is required.
		found = $1.to_s.gsub("\t", "    ").size

		if found >= @indentation
			# Tab-friendly way to remove indentation.
			remaining = @indentation
			line = line.dup
			while remaining > 0
				if line[0..0] == " "
					remaining -= 1
				else
					# This is a tab.
					remaining -= 4
				end
				line.slice!(0, 1)
			end
			return line
		else
			terminate "wrong indentation: found #{found} characters, should be at least #{@indentation}"
		end
	end

	def debug(message)
		puts "DEBUG:#{@lineno}: #{message}" if @debug
	end

	def terminate(message)
		abort "*** ERROR: #{@filename} line #{@lineno}: #{message}"
	end
end
