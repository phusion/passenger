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

  # Core of the `passenger-config` command. Dispatches a subcommand to a specific class.
  module Config
    KNOWN_COMMANDS = [
      ["detach-process", "DetachProcessCommand"],
      ["restart-app", "RestartAppCommand"],
      ["list-instances", "ListInstancesCommand"],
      ["reopen-logs", "ReopenLogsCommand"],
      ["api-call", "ApiCallCommand"],
      ["validate-install", "ValidateInstallCommand"],
      ["build-native-support", "BuildNativeSupportCommand"],

      ["install-agent", "InstallAgentCommand"],
      ["install-standalone-runtime", "InstallStandaloneRuntimeCommand"],
      ["download-agent", "DownloadAgentCommand"],
      ["download-nginx-engine", "DownloadNginxEngineCommand"],
      ["compile-agent", "CompileAgentCommand"],
      ["compile-nginx-engine", "CompileNginxEngineCommand"],

      ["system-metrics", "SystemMetricsCommand"],
      ["system-properties", "SystemPropertiesCommand"],
      ["about", "AboutCommand"]
    ]

    ABOUT_OPTIONS = [
      "root",
      "includedir",
      "nginx-addon-dir",
      "nginx-libs",
      "nginx-dynamic-libs",
      "nginx-dynamic-compiled",
      "compiled",
      "custom-packaged",
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
      elsif help_all_requested?(argv)
        help(true)
      elsif command_class
        command = command_class.new(new_argv)
        command.run
      else
        help
        abort
      end
    end

    def self.help(all = false)
      puts "Usage: passenger-config <COMMAND> [OPTIONS...]"
      puts
      puts "  Tool for managing, controlling and configuring a #{PROGRAM_NAME} instance"
      puts "  or installation."
      puts
      puts "Management commands:"
      puts "  detach-process         Detach an application process from the process pool"
      puts "  restart-app            Restart an application"
      puts "  reopen-logs            Instruct #{PROGRAM_NAME} agents to reopen their log"
      puts "                         files"
      puts "  api-call               Makes an API call to a #{PROGRAM_NAME} agent."
      puts
      puts "Informational commands:"
      puts "  list-instances         List running #{PROGRAM_NAME} instances"
      puts "  about                  Show information about #{PROGRAM_NAME}"
      puts
      puts "#{PROGRAM_NAME} installation management:"
      puts "  validate-install       Validate this #{PROGRAM_NAME} installation"
      puts "  build-native-support   Ensure that the native_support library for the current"
      puts "                         Ruby interpreter is built"
      puts "  install-agent          Install the #{PROGRAM_NAME} agent binary"
      puts "  install-standalone-runtime"
      puts "                         Install the #{PROGRAM_NAME} Standalone"
      puts "                         runtime"
      if all
        puts "  download-agent         Download the #{PROGRAM_NAME} agent binary"
        puts "  download-nginx-engine  Download the Nginx engine for use with"
        puts "                         #{PROGRAM_NAME} Standalone"
        puts "  compile-agent          Compile the #{PROGRAM_NAME} agent binary"
        puts "  compile-nginx-engine   Compile an Nginx engine for use with #{PROGRAM_NAME}"
        puts "                         Standalone"
      end
      puts
      puts "Miscellaneous commands:"
      puts "  system-metrics        Display system metrics"
      puts "  system-properties     Display system properties"
      puts
      puts "Run 'passenger-config <COMMAND> --help' for more information about each"
      puts "command."
      if !all
        puts
        puts "There are also some advanced commands not shown in this help message. Run"
        puts "'passenger-config --help-all' to learn more about them."
      end
    end

  private
    def self.help_requested?(argv)
      return argv.size == 1 && (argv[0] == "--help" || argv[0] == "-h" || argv[0] == "help")
    end

    def self.help_all_requested?(argv)
      return argv.size == 1 && (argv[0] == "--help-all" || argv[0] == "help-all")
    end

    def self.lookup_command_class_by_argv(argv)
      return nil if argv.empty?

      # Compatibility with version <= 4.0.29: try to pass all
      # --switch invocations to AboutCommand.
      if argv[0] =~ /^--/
        name = argv[0].sub(/^--/, '')
        if ABOUT_OPTIONS.include?(name)
          command_class = lookup_command_class_by_class_name("AboutCommand")
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
        "_" + match[0..0].downcase
      end
      base_name.sub!(/^_/, '')
      base_name << ".rb"
      PhusionPassenger.require_passenger_lib("config/#{base_name}")
      return PhusionPassenger::Config.const_get(class_name)
    end
  end

end # module PhusionPassenger
