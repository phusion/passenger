#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2013-2014 Phusion
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

require 'optparse'
PhusionPassenger.require_passenger_lib 'constants'
PhusionPassenger.require_passenger_lib 'admin_tools/server_instance'
PhusionPassenger.require_passenger_lib 'config/command'
PhusionPassenger.require_passenger_lib 'config/utils'

module PhusionPassenger
module Config

class RestartAppCommand < Command
	include PhusionPassenger::Config::Utils

	def run
		parse_options
		select_passenger_instance
		@admin_client = connect_to_passenger_admin_socket(:role => :passenger_status)
		select_app_group_name
		perform_restart
	end

private
	def self.create_option_parser(options)
		OptionParser.new do |opts|
			nl = "\n" + ' ' * 37
			opts.banner =
				"Usage 1: passenger-config restart-app <APP PATH PREFIX> [OPTIONS]\n" +
				"Usage 2: passenger-config restart-app --name <APP GROUP NAME> [OPTIONS]"
			opts.separator ""
			opts.separator "  Restart an application. The syntax determines how the application that is to"
			opts.separator "  be restarted, will be selected."
			opts.separator ""
			opts.separator "  1. Selects all applications whose paths begin with the given prefix."
			opts.separator ""
			opts.separator "     Example: passenger-config restart-app /webapps"
			opts.separator "     Restarts all apps whose path begin with /webapps, such as /webapps/foo,"
			opts.separator "     /webapps/bar and /webapps123."
			opts.separator ""
			opts.separator "  2. Selects a specific application based on an exact match of its app group"
			opts.separator "     name."
			opts.separator ""
			opts.separator "     Example: passenger-config restart-app --name /webapps/foo"
			opts.separator "     Restarts only /webapps/foo, but not for example /webapps/foo/bar or"
			opts.separator "     /webapps/foo123."
			opts.separator ""

			opts.separator "Options:"
			opts.on("--name APP_GROUP_NAME", String, "The app group name to select") do |value|
				options[:app_group_name] = value
			end
			opts.on("--rolling-restart", "Perform a rolling restart instead of a#{nl}" +
				"regular restart (Enterprise only). The#{nl}" +
				"default is a blocking restart") do |value|
				if Config::Utils.is_enterprise?
					options[:rolling_restart] = true
				else
					abort "--rolling-restart is only available in #{PROGRAM_NAME} Enterprise: #{ENTERPRISE_URL}"
				end
			end
			opts.on("--ignore-app-not-running", "Exit successfully if the specified#{nl}" +
				"application is not currently running. The#{nl}" +
				"default is to exit with an error") do
				options[:ignore_app_not_running] = true
			end
			opts.on("--instance PID", Integer, "The #{PROGRAM_NAME} instance to select") do |value|
				options[:instance] = value
			end
			opts.on("-h", "--help", "Show this help") do
				options[:help] = true
			end
		end
	end

	def help
		puts @parser
	end

	def parse_options
		super
		case @argv.size
		when 0
			if !@options[:app_group_name]
				abort "Please pass either an app path prefix or an app group name. " +
					"See --help for more information."
			end
		when 1
			if @options[:app_group_name]
				abort "You've passed an app path prefix, but you cannot also pass an " +
					"app group name. Please use only either one of them. See --help " +
					"for more information."
			end
		else
			help
			abort
		end
	end

	def select_app_group_name
		groups = @server_instance.groups(@admin_client)
		if app_group_name = @options[:app_group_name]
			@groups = [groups.find { |g| g.name == app_group_name }]
			if !@groups[0]
				abort_app_not_found "There is no #{PROGRAM_NAME}-served application running with the app group name '#{app_group_name}'."
			end
		else
			regex = /^#{Regexp.escape(@argv.first)}/
			@groups = groups.find_all { |g| g.app_root =~ regex }
			if @groups.empty?
				abort_app_not_found "There are no #{PROGRAM_NAME}-served applications running whose paths begin with '#{@argv.first}'."
			end
		end
	end

	def perform_restart
		restart_method = @options[:rolling_restart] ? "rolling" : "blocking"
		@groups.each do |group|
			puts "Restarting #{group.name}"
			@admin_client.restart_app_group(group.name,
				:method => restart_method)
		end
	end

	def abort_app_not_found(message)
		if @options[:ignore_app_not_running]
			STDERR.puts(message)
			exit
		else
			abort(message)
		end
	end
end

end # module Config
end # module PhusionPassenger
