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
PhusionPassenger.require_passenger_lib 'constants'
PhusionPassenger.require_passenger_lib 'config/command'
PhusionPassenger.require_passenger_lib 'config/nginx_engine_compiler'
PhusionPassenger.require_passenger_lib 'utils/ansi_colors'

module PhusionPassenger
  module Config

    class CompileNginxEngineCommand < Command
      include InstallationUtils

      def run
        @options = {
          :colorize => :auto,
          :force_tip => true,
          :connect_timeout => 30,
          :idle_timeout => 30
        }
        parse_options
        initialize_objects
        sanity_check
        if !NginxEngineCompiler.new(@options).run
          abort
        end
      end

    private
      def self.create_option_parser(options)
        OptionParser.new do |opts|
          nl = "\n" + ' ' * 37
          opts.banner = "Usage: passenger-config compile-nginx-engine [OPTIONS]\n"
          opts.separator ""
          opts.separator "  Compile an Nginx engine, for use in #{PROGRAM_NAME} Standalone."
          opts.separator ""

          opts.separator "Options:"
          opts.on("--working-dir PATH", String, "Store temporary files in the given#{nl}" +
            "directory, instead of creating one") do |val|
            options[:working_dir] = val
          end
          opts.on("--nginx-version VERSION", String, "Nginx version to compile. " +
            "Default: #{PREFERRED_NGINX_VERSION}") do |val|
            options[:nginx_version] = val
          end
          opts.on("--nginx-tarball PATH", String, "Use the given Nginx tarball instead of#{nl}" +
            "downloading it. You MUST also specify the#{nl}" +
            "Nginx version with --nginx-version") do |val|
            options[:nginx_tarball] = val
          end
          opts.on("-f", "--force", "Skip sanity checks") do
            options[:force] = true
          end
          opts.on("--no-force-tip", "Do not print any tips regarding the --force#{nl}" +
            "parameter") do
            options[:force_tip] = false
          end
          opts.on("--connect-timeout SECONDS", Integer,
            "The maximum amount of time to spend on DNS#{nl}" +
            "lookup and establishing the TCP connection.#{nl}" +
            "Default: 30") do |val|
            options[:connect_timeout] = val
          end
          opts.on("--idle-timeout SECONDS", Integer, "The maximum idle read time. Default: 30") do |val|
            options[:idle_timeout] = val
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
        if !@options[:nginx_version]
          if @options[:nginx_tarball]
            abort "#{@colors.red}Error: if you specify --nginx-tarball, " +
              "you must also specify --nginx-version.#{@colors.reset}"
          else
            @options[:nginx_version] = PREFERRED_NGINX_VERSION
          end
        end
      end

      def sanity_check
        return if @options[:force]

        if PhusionPassenger.find_support_binary("nginx-#{@options[:nginx_version]}")
          puts "#{@colors.green}The Nginx engine (version #{@options[:nginx_version]}) " +
            "is already installed.#{@colors.reset}"
          if @options[:force_tip]
            puts "If you want to recompile it, re-run this program with the --force parameter."
          end
          exit
        end
      end
    end

  end # module Config
end # module PhusionPassenger
