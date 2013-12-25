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

PhusionPassenger.require_passenger_lib 'constants'

module PhusionPassenger

# Core of the `passenger-config` command. Dispatches a subcommand to a specific class.
module Config
	KNOWN_COMMANDS = [
		["restart", "RestartCommand"],
		["info", "InfoCommand"]
	]
	
	INFO_OPTIONS = [
		"root",
		"includedir",
		"nginx-addon-dir",
		"nginx-libs",
		"compiled",
		"natively-packaged",
		"installed-from-release-package",
		"make-locations-ini",
		"detect-apache2",
		"ruby-command",
		"ruby-libdir",
		"rubyext-compat-id",
		"cxx-compat-id",
		"version"
	]

	def self.run!(argv)
		command_class, new_argv = lookup_command_class_by_argv(argv)
		if help_requested?(argv)
			help
		elsif command_class
			command = command_class.new(new_argv)
			command.run
		else
			help
			abort
		end
	end

	def self.help
		puts "Usage: passenger-config <COMMAND> [options]"
		puts "Tool for controlling or configurating a #{PROGRAM_NAME} instance or installation."
		puts
		puts "Available commands:"
		KNOWN_COMMANDS.each do |props|
			command_class = lookup_command_class_by_class_name(props[1])
			printf "  %-15s %s\n", props[0], command_class.description
		end
		puts
		puts "Type 'passenger-config help <COMMAND>' for more information."
	end

private
	def self.help_requested?(argv)
		return argv.size == 1 && (argv[0] == "--help" || argv[0] == "-h" || argv[0] == "help")
	end

	def self.lookup_command_class_by_argv(argv)
		return nil if argv.empty?

		# Compatibility with version <= 4.0.29: try to pass all
		# --switch invocations to InfoCommand.
		if argv[0] =~ /^--/
			name = argv[0].sub(/^--/, '')
			if INFO_OPTIONS.include?(name)
				command_class = lookup_command_class_by_class_name("InfoCommand")
				return [command_class, argv]
			else
				return nil
			end
		end

		# Convert "passenger-config help <COMMAND>" to "passenger-config <COMMAND> --help".
		if argv.size == 2 && argv[0] == "help"
			argv = [argv[1], "--help"]
		end

		KNOWN_COMMANDS.each do |props|
			if argv[0] == props[0]
				command_class = lookup_command_class_by_class_name(props[1])
				new_argv = argv[1 .. -1]
				return [command_class, new_argv]
			end
		end

		return nil
	end

	def self.lookup_command_class_by_class_name(class_name)
		base_name = class_name.gsub(/[A-Z]/) do |match|
			"_" + match[0].downcase
		end
		base_name.sub!(/^_/, '')
		base_name << ".rb"
		PhusionPassenger.require_passenger_lib("config/#{base_name}")
		return PhusionPassenger::Config.const_get(class_name)
	end
end

end # module PhusionPassenger
