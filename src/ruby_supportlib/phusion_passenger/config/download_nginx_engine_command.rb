#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2014-2018 Phusion Holding B.V.
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
PhusionPassenger.require_passenger_lib 'config/installation_utils'
PhusionPassenger.require_passenger_lib 'platform_info'
PhusionPassenger.require_passenger_lib 'platform_info/binary_compatibility'
PhusionPassenger.require_passenger_lib 'utils/download'
PhusionPassenger.require_passenger_lib 'utils/ansi_colors'
PhusionPassenger.require_passenger_lib 'utils/shellwords'
PhusionPassenger.require_passenger_lib 'utils/tmpio'

module PhusionPassenger
  module Config

    class DownloadNginxEngineCommand < Command
      include InstallationUtils

      BINARY_NOT_USABLE_EXIT_CODE = 3

      def run
        @options = {
          :log_level => Logger::INFO,
          :colors => :auto,
          :error_colors => true,
          :show_download_progress => STDOUT.tty?,
          :compilation_tip => true,
          :force_tip => true,
          :use_cache => true,
          :connect_timeout => 30,
          :idle_timeout => 30
        }
        parse_options
        initialize_objects
        sanity_check
        download_and_extract
      end

    private
      def self.create_option_parser(options)
        OptionParser.new do |opts|
          nl = "\n" + ' ' * 37
          opts.banner = "Usage: passenger-config download-nginx-engine [OPTIONS]\n"
          opts.separator ""
          opts.separator "  Download an Nginx #{PREFERRED_NGINX_VERSION} engine " +
            "from the #{PROGRAM_NAME} website,"
          opts.separator "  for use with #{PROGRAM_NAME} Standalone. Precompiled engines are only"
          opts.separator "  available for Linux and OS X."
          opts.separator ""
          opts.separator "  It is not possible to customize the Nginx engine version. " +
            "It must be #{PREFERRED_NGINX_VERSION}."
          opts.separator ""

          opts.separator "Options:"
          opts.on("--url-root URL", String, "Download the engine from a custom URL") do |value|
            options[:url_root] = value
          end
          opts.on("--log-prefix PREFIX", String, "Prefix all logs with the given string") do |value|
            options[:log_prefix] = value
          end
          opts.on("--log-level LEVEL", String, "Set log level (fatal,error,warn,info,#{nl}" +
            "debug). Default: info") do |value|
            case value
            when "fatal"
              options[:log_level] = Logger::FATAL
            when "error"
              options[:log_level] = Logger::ERROR
            when "warn"
              options[:log_level] = Logger::WARN
            when "info"
              options[:log_level] = Logger::INFO
            when "debug"
              options[:log_level] = Logger::DEBUG
            else
              abort "Invalid log level #{value.inspect}"
            end
          end
          opts.on("-f", "--force", "Skip sanity checks") do
            options[:force] = true
          end
          opts.on("--no-colors", "Never output colors") do
            options[:colors] = false
          end
          opts.on("--no-error-colors", "Do not colorized error messages") do
            options[:error_colors] = false
          end
          opts.on("--no-download-progress", "Never show download progress") do
            options[:show_download_progress] = false
          end
          opts.on("--no-compilation-tip", "Do not present compilation as an#{nl}" +
            "alternative way to install the agent") do
            options[:compilation_tip] = false
          end
          opts.on("--no-force-tip", "Do not print any tips regarding the#{nl}" +
            "--force parameter") do
            options[:force_tip] = false
          end
          opts.on("--skip-cache", "Do not copy the Nginx engine from cache") do
            options[:use_cache] = false
          end
          opts.on("--suppress-binary-unusable-message",
            "Do not print anything if the downloaded#{nl}" +
            "binary turns out to be unusable") do
            options[:suppress_binary_unusable_message] = true
          end
          opts.on("--dry-run", "Do everything except actually installing#{nl}" +
            "the engine") do
            options[:dry_run] = true
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
        if @options[:url_root]
          @sites = [{ :url => @options[:url_root] }]
        else
          @sites = PhusionPassenger.binaries_sites
        end
        @colors = Utils::AnsiColors.new(@options[:colors])
        @logger = Logger.new(STDOUT)
        @logger.level = @options[:log_level]
        @logger.formatter = proc do |severity, datetime, progname, msg|
          if @options[:error_colors] && (severity == "FATAL" || severity == "ERROR")
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

        if PhusionPassenger.find_support_binary("nginx-#{PREFERRED_NGINX_VERSION}")
          @logger.warn "#{@colors.green}The Nginx engine (version #{PREFERRED_NGINX_VERSION}) is already installed."
          if @options[:force_tip]
            @logger.warn "If you want to redownload it, re-run this program with the --force parameter."
          end
          exit
        end

        if !PhusionPassenger.installed_from_release_package?
          @logger.fatal("#{PROGRAM_NAME} was not installed from an official release " +
            "package, so you cannot download our precompiled Nginx engine." +
            compile_tip_message)
          if @options[:force_tip]
            @logger.warn "If you want to download it anyway, re-run this program with the --force parameter."
          end
          abort
        end

        check_for_download_tool!
      end

      def download_and_extract
        destdir = find_or_create_writable_support_binaries_dir!
        exit if @options[:dry_run]

        PhusionPassenger::Utils.mktmpdir("passenger-install.", PlatformInfo.tmpexedir) do |tmpdir|
          basename = "nginx-#{PREFERRED_NGINX_VERSION}-#{PlatformInfo.cxx_binary_compatibility_id}.tar.gz"
          tarball  = "#{tmpdir}/#{basename}"
          if !download_support_file(basename, tarball)
            @logger.error "#{@colors.reset}------------------------------------------"
            @logger.fatal("Sorry, no precompiled Nginx #{PREFERRED_NGINX_VERSION} engine is available for " +
              "your platform." + compile_tip_message)
            abort
          end

          @logger.info "Extracting precompiled Nginx #{PREFERRED_NGINX_VERSION} engine to #{destdir}"
          e_tmpdir = Shellwords.escape(tmpdir)
          e_tarball = Shellwords.escape(tarball)
          if !system("cd #{e_tmpdir} && tar xzf #{e_tarball}")
            @logger.fatal "The downloaded archive file could not be extracted."
            abort
          end
          if !File.exist?("#{tmpdir}/nginx-#{PREFERRED_NGINX_VERSION}")
            @logger.fatal "The downloaded archive file does not seem to " +
              "contain an Nginx #{PREFERRED_NGINX_VERSION} engine. This is probably " +
              "a problem in the #{PROGRAM_NAME} website. Please report this problem to " +
              "the #{PROGRAM_NAME} authors."
            abort
          end

          @logger.info "Checking whether the downloaded engine is usable..."
          if test_binary("#{tmpdir}/nginx-#{PREFERRED_NGINX_VERSION}")
            @logger.info "The downloaded engine is usable."
          else
            if !@options[:suppress_binary_unusable_message]
              @logger.fatal "Sorry, the precompiled Nginx engine can not be run " +
                "your system.#{compile_tip_message}"
            end
            exit(BINARY_NOT_USABLE_EXIT_CODE)
          end

          FileUtils.mv("#{tmpdir}/nginx-#{PREFERRED_NGINX_VERSION}",
            "#{destdir}/nginx-#{PREFERRED_NGINX_VERSION}")
          @logger.info "#{@colors.green}Nginx #{PREFERRED_NGINX_VERSION} engine successfully download and installed."
        end
      end

      def download_support_file(name, output)
        @sites.each_with_index do |site, i|
          if real_download_support_file(site, name, output)
            if i > 0
              @logger.warn "#{@colors.green}Download OK!"
            else
              @logger.info "#{@colors.green}Download OK!"
            end
            return true
          elsif i != @sites.size - 1
            @logger.warn "Trying next mirror..."
          end
        end
        return false
      end

      def real_download_support_file(site, name, output)
        if site[:url].include?('{{VERSION}}')
          url = site[:url].gsub('{{VERSION}}', VERSION_STRING) + "/#{name}"
        else
          url = "#{site[:url]}/#{VERSION_STRING}/#{name}"
        end
        options = {
          :cacert => site[:cacert],
          :logger => @logger,
          :use_cache => @options[:use_cache],
          :show_progress   => @options[:show_download_progress]
        }
        # connect_timeout and idle_timeout may be nil or 0, which means
        # that the default Utils::Download timeouts should be used.
        if @options[:connect_timeout] && @options[:connect_timeout] != 0
          options[:connect_timeout] = @options[:connect_timeout]
        end
        if @options[:idle_timeout] && @options[:idle_timeout] != 0
          options[:idle_timeout] = @options[:idle_timeout]
        end
        return PhusionPassenger::Utils::Download.download(url, output, options)
      end

      def test_binary(filename)
        output = `env LD_BIND_NOW=1 DYLD_BIND_AT_LAUNCH=1 #{Shellwords.escape(filename)} -v 2>&1`
        return $? && $?.exitstatus == 0 && output =~ /nginx version:/
      end

      def compile_tip_message
        return "" if !@options[:compilation_tip]
        if PhusionPassenger.build_system_dir
          result = " Please compile the Nginx engine from source instead, by running:\n\n"
          result << "  passenger-config compile-nginx-engine"
        else
          result = " Furthermore, this #{PROGRAM_NAME} installation does not "
          result << "come with any source code, so the Nginx engine cannot "
          result << "be compiled either. Please contact the person or "
          result << "organization who packaged #{PROGRAM_NAME} for help on this "
          result << "problem."
        end
        return result
      end
    end

  end # module Config
end # module PhusionPassenger
