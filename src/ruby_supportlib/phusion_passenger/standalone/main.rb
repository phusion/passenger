#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2013-2017 Phusion Holding B.V.
#
#  "Passenger", "Phusion Passenger" and "Union Station" are registered
#  trademarks of Phusion Holding B.V.
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

  # Core of the `passenger` command (Passenger Standalone). Dispatches a subcommand to a specific class.
  module Standalone
    KNOWN_COMMANDS = [
      ["start", "StartCommand"],
      ["stop", "StopCommand"],
      ["status", "StatusCommand"],
      ["version", "VersionCommand"]
    ]

    def self.run!(argv)
      if argv.empty?
        help
        exit
      end

      command_class, new_argv = lookup_command_class_by_argv(argv)
      if help_requested?(argv)
        help
      elsif version_requested?(argv)
        show_version
      elsif command_class
        command = command_class.new(new_argv)
        command.run
      else
        help
        abort
      end
    end

    def self.help
      puts "#{PROGRAM_NAME} Standalone, the easiest way to run web apps."
      puts
      puts "Available commands:"
      puts
      puts "  passenger start      Start #{PROGRAM_NAME} Standalone."
      puts "  passenger stop       Stop a #{PROGRAM_NAME} instance."
      puts "  passenger status     Show the status of a running #{PROGRAM_NAME} instance."
      puts
      puts "Run 'passenger <COMMAND> --help' for more information about each command."
    end

  private
    def self.help_requested?(argv)
      return argv.size == 1 && (argv[0] == "--help" || argv[0] == "-h" || argv[0] == "help")
    end

    def self.version_requested?(argv)
      return argv.size == 1 && (argv[0] == "--version" || argv[0] == "-v")
    end

    def self.show_version
      command_class, new_argv = lookup_command_class_by_argv(["version"])
      command_class.new(new_argv).run
    end

    def self.lookup_command_class_by_argv(argv)
      return nil if argv.empty?

      # Convert "passenger help <COMMAND>" to "passenger <COMMAND> --help".
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
        "_" + match[0..0].downcase
      end
      base_name.sub!(/^_/, '')
      base_name << ".rb"
      PhusionPassenger.require_passenger_lib("standalone/#{base_name}")
      return PhusionPassenger::Standalone.const_get(class_name)
    end
  end

end # module PhusionPassenger
