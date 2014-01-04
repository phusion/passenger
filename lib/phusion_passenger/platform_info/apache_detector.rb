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
PhusionPassenger.require_passenger_lib 'platform_info'
PhusionPassenger.require_passenger_lib 'platform_info/ruby'
PhusionPassenger.require_passenger_lib 'platform_info/apache'
PhusionPassenger.require_passenger_lib 'utils/ansi_colors'
require 'pathname'

module PhusionPassenger
module PlatformInfo

# Detects all possible Apache installations on the system, and presents the
# autodetection information to the user in a friendly way. It turns out too
# many people have multiple Apache installations on their system, but they
# don't know about that, or they don't know how to compile against the
# correct Apache installation. This tool helps them.
# 
# If you use this class to log things to the terminal, then be sure to set
# the terminal color to Utils::AnsiColors::DEFAULT_TERMINAL_COLOR.
class ApacheDetector
	class Result
		# These are required and are never nil.
		attr_accessor :apxs2, :httpd, :ctl, :version, :config_file
		# These are optional and may be nil.
		attr_accessor :a2enmod, :a2dismod
		# This may be nil. It depends on how well we can infer information from the config file.
		attr_accessor :error_log

		def initialize(detector)
			@detector = detector
		end

		def report
			log " <b>* Found Apache #{version}!</b>"
			log "   Information:"
			log "      apxs2          : #{apxs2}"
			log "      Main executable: #{httpd}"
			log "      Control command: #{ctl}"
			log "      Config file    : #{config_file}"
			log "      Error log file : #{error_log || 'unknown'}"
			log ""
			log "   To install #{PROGRAM_NAME} against this specific Apache version:"
			log "      #{PlatformInfo.ruby_command} #{PhusionPassenger.bin_dir}/passenger-install-apache2-module --apxs2-path='#{apxs2}'"
			log ""
			log "   To start, stop or restart this specific Apache version:"
			log "      #{ctl} start"
			log "      #{ctl} stop"
			log "      #{ctl} restart"
			log ""
			if error_log
				log "   To troubleshoot, please read the logs in this file:"
				log "      #{error_log}"
				log ""
			end
		end

	private
		def log(message)
			@detector.send(:log, message)
		end
	end

	attr_reader :results

	def initialize(output)
		@output  = output
		@results = []
		PlatformInfo.verbose = true
		PlatformInfo.log_implementation = lambda do |message|
			if message =~ /: found$/
				log("<green> --> #{message}</green>")
			else
				log(" --> #{message}")
			end
		end
	end

	def finish
		PlatformInfo.verbose = false
		PlatformInfo.log_implementation = nil
	end

	def detect_all
		log "<banner>Looking for possible Apache installations...</banner>"
		apxses = PlatformInfo.find_all_commands("apxs2") +
			PlatformInfo.find_all_commands("apxs")
		apxses = remove_symlink_duplications(apxses)
		log ""
		apxses.each do |apxs2|
			detect_one(apxs2)
		end
	end

	def detect_one(apxs2)
		log "<banner>Analyzing #{apxs2}...</banner>"
		add_result do |result|
			result.apxs2 = apxs2
			log "Detecting main Apache executable..."
			result.httpd = PlatformInfo.httpd(:apxs2 => apxs2)
			if result.httpd
				log "Detecting version..."
				if result.version = PlatformInfo.httpd_version(:httpd => result.httpd)
					log " --> #{result.version}"
				else
					log "<red> --> Cannot detect version!</red>"
					result.httpd = nil
				end
			end
			if result.httpd
				log "Detecting control command..."
				result.ctl = PlatformInfo.apache2ctl(:apxs2 => apxs2)
				result.httpd = nil if !result.ctl
			end
			if result.httpd
				log "Detecting configuration file location..."
				result.config_file = PlatformInfo.httpd_default_config_file(:httpd => result.httpd)
				if result.config_file
					log " --> #{result.config_file}"
				else
					log "<red> --> Cannot detect default config file location!</red>"
					result.httpd = nil
				end
			end
			if result.httpd
				log "Detecting error log file..."
				result.error_log = PlatformInfo.httpd_actual_error_log(:httpd => result.httpd)
				if result.error_log
					log " --> #{result.error_log}"
				else
					log "<red> --> Cannot detect error log file!</red>"
				end
			end
			if result.httpd
				log "Detecting a2enmod and a2dismod..."
				result.a2enmod = PlatformInfo.a2enmod(:apxs2 => apxs2)
				result.a2dismod = PlatformInfo.a2dismod(:apxs2 => apxs2)
			end
			if result.httpd
				log "<green>Found a usable Apache installation using #{apxs2}.</green>"
				true
			else
				log "<yellow>Cannot find a usable Apache installation using #{apxs2}.</yellow>"
				false
			end
		end
		log ""
	end

	def report
		log "<banner>Final autodetection results</banner>"
		@results.each do |result|
			result.report
		end
		if @results.empty?
			log "<red>Sorry, this program cannot find an Apache installation.</red>"
			log ""
			log "To install Apache, please run the following. It will tell you how to install Apache."
			log ""
			log "   <b>#{PhusionPassenger.bin_dir}/passenger-install-apache2-module</b>"
			log ""
			log "If you are sure that you have Apache installed, please read the documentation:"
			log "<b>#{APACHE2_DOC_URL}#forcing_location_of_command_line_tools_and_dependencies</b>"
		elsif @results.size > 1
			log "<yellow>WARNING: You have multiple Apache installations on your system!</yellow>"
			log "You are strongly recommended to read this section of the documentation:"
			log "<b>#{APACHE2_DOC_URL}#multiple_apache_installs</b>"
		end
	end

	def result_for(apxs2)
		# All the results use realpaths, so the input must too.
		apxs2 = Pathname.new(apxs2).realpath
		return @results.find { |r| r.apxs2 == apxs2 }
	end

private
	def log(message)
		if @output.tty?
			@output.puts(Utils::AnsiColors.ansi_colorize(message))
		else
			@output.puts(Utils::AnsiColors.strip_color_tags(message))
		end
	end

	# On Ubuntu, /usr/bin/apxs2 is a symlink to /usr/bin/apxs.
	# On recent Arch Linux releases, /bin, /sbin etc are symlinks to
	# /usr/bin and /usr/sbin.
	# We're only supposed to detect one Apache in that case so we need to
	# resolve symlinks.
	def remove_symlink_duplications(filenames)
		old_size = filenames.size
		filenames = filenames.map do |filename|
			Pathname.new(filename).realpath
		end
		filenames.uniq!
		if old_size != filenames.size
			log "#{old_size - filenames.size} symlink duplicate(s) detected; ignoring them."
		end
		return filenames
	end

	def add_result
		result = Result.new(self)
		@results << result if yield(result)
	end
end

end
end # module Phusion Passenger
