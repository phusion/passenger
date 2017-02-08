#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2014-2017 Phusion Holding B.V.
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

require 'optparse'
require 'logger'
PhusionPassenger.require_passenger_lib 'constants'
PhusionPassenger.require_passenger_lib 'config/command'
PhusionPassenger.require_passenger_lib 'config/download_agent_command'
PhusionPassenger.require_passenger_lib 'config/compile_agent_command'
PhusionPassenger.require_passenger_lib 'utils/ansi_colors'

module PhusionPassenger
  module Config

    class InstallAgentCommand < Command
      def run
        @options = {
          :log_level => Logger::INFO,
          :colorize => :auto,
          :force => false,
          :force_tip => true,
          :compile => true,
          :download_args => [
            "--no-error-colors",
            "--no-compilation-tip"
          ],
          :compile_args => []
        }
        parse_options
        initialize_objects
        sanity_check
        if !download
          compile
        end
      end

    private
      def self.create_option_parser(options)
        OptionParser.new do |opts|
          nl = "\n" + ' ' * 37
          opts.banner = "Usage: passenger-config install-agent [OPTIONS]\n"
          opts.separator ""
          opts.separator "  Install the #{PROGRAM_NAME} agent binary. The agent binary is required for"
          opts.separator "  #{PROGRAM_NAME} to function properly. Installation is done either by"
          opts.separator "  downloading it from the #{PROGRAM_NAME} website, or by compiling it from"
          opts.separator "  source."
          opts.separator ""

          opts.separator "Options:"
          opts.on("--working-dir PATH", String, "Store temporary files in the given#{nl}" +
            "directory, instead of creating one") do |val|
            options[:compile_args] << "--working-dir"
            options[:compile_args] << val
          end
          opts.on("--url-root URL", String, "Download the binary from a custom URL") do |value|
            options[:download_args] << "--url-root"
            options[:download_args] << value
          end
          opts.on("--brief", "Report progress in a brief style") do
            options[:brief] = true
            options[:download_args] << "--log-level"
            options[:download_args] << "warn"
            options[:download_args] << "--log-prefix"
            options[:download_args] << "     "
            options[:download_args] << "--no-download-progress"
          end
          opts.on("--auto", "Run in non-interactive mode. Default when#{nl}" +
            "stdin or stdout is not a TTY") do
            options[:compile_args] << "--auto"
          end
          opts.on("-f", "--force", "Skip sanity checks") do
            options[:force] = true
            options[:download_args] << "--force"
            options[:compile_args] << "--force"
          end
          opts.on("--no-force-tip", "Do not print any tips regarding the#{nl}" +
            "--force parameter") do
            options[:force_tip] = false
            options[:download_args] << "--no-force-tip"
            options[:compile_args] << "--no-force-tip"
          end
          opts.on("--no-compile", "Download, but do not compile") do
            options[:compile] = false
          end
          opts.on("--skip-cache", "Do not copy the agent binary from cache") do
            options[:download_args] << "--skip-cache"
          end
          opts.on("--connect-timeout SECONDS", Integer,
            "The maximum amount of time to spend on DNS#{nl}" +
            "lookup and establishing the TCP connection.#{nl}" +
            "Default: 30") do |val|
            options[:download_args] << "--connect-timeout"
            options[:download_args] << val.to_s
          end
          opts.on("--idle-timeout SECONDS", Integer, "The maximum idle read time. Default: 30") do |val|
            options[:download_args] << "--idle-timeout"
            options[:download_args] << val.to_s
          end
          opts.on("-h", "--help", "Show this help") do
            options[:help] = true
          end
        end
      end

      def help
        puts @parser
      end

      def initialize_objects
        @colors = Utils::AnsiColors.new(@options[:colorize])
        @logger = Logger.new(STDOUT)
        @logger.level = @options[:log_level]
        @logger.formatter = proc do |severity, datetime, progname, msg|
          if severity == "FATAL" || severity == "ERROR"
            color = @colors.red
          else
            color = nil
          end
          result = ""
          msg.split("\n", -1).map do |line|
            result << "#{color}#{@options[:log_prefix]}#{line}#{@colors.reset}\n"
          end
          result
        end
      end

      def sanity_check
        return if @options[:force]

        if PhusionPassenger.find_support_binary(AGENT_EXE)
          @logger.warn "#{@colors.green}The #{PROGRAM_NAME} agent is already installed."
          if @options[:force_tip]
            @logger.warn "If you want to redownload it, re-run this program with the --force parameter."
          end
          exit
        end
      end

      def download
        if @options[:brief]
          puts " --> Downloading a #{PROGRAM_NAME} agent binary for your platform"
        else
          puts "#{@colors.blue_bg}#{@colors.yellow}#{@colors.bold}Downloading a #{PROGRAM_NAME} agent " +
            "binary for your platform#{@colors.reset}"
          puts
        end
        begin
          DownloadAgentCommand.new(@options[:download_args]).run
          return true
        rescue SystemExit => e
          return e.success?
        end
      end

      def compile
        puts
        puts "---------------------------------------"
        puts
        if @options[:compile]
          puts "The #{PROGRAM_NAME} agent binary could not be downloaded. Compiling it from source instead."
          puts
          CompileAgentCommand.new(@options[:compile_args]).run
        else
          abort "No precompiled agent binary could be downloaded. Refusing to compile because --no-compile is given."
        end
      end
    end

  end # module Config
end # module PhusionPassenger
