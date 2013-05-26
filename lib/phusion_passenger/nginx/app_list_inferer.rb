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

# Infers a list of applications and their options from a parsed Nginx configuration file.
class AppListInferer
	def initialize(config_root)
		@config_root = config_root
		index_relevant_options
	end

	def start
		@config_root.build_index
		http = @config_root.block('http')
		return {} if !http

		resolve_aliases(http)
		http.build_index
		http.find_blocks('server').each do |server|
			resolve_aliases(server)
			server.build_index
			merge_options(server, http)

			server.find_blocks('location').each do |location|
				resolve_aliases(location)
				server.build_index
				merge_options(location, server)
			end
		end

		apps = {}
		http.find_blocks('server').each do |server|
			register_app(apps, server)
			server.find_blocks('location').each do |location|
				register_app(apps, location)
			end
		end
		return apps
	end

private
	def index_relevant_options
		@supported_options = {}
		@mergeable_options = { :root => true }

		options = LOCATION_CONFIGURATION_OPTIONS.reject do |option|
			option.fetch(:field, true).nil? ||
				option[:field].to_s =~ /\./ ||
				!option.fetch(:header, true)
		end
		options.each do |option|
			@supported_options[option[:name]] = option
			@mergeable_options[option[:name]] = true
		end
	end

	def resolve_aliases(context)
		context.directives.each do |directive|
			next if directive[0] != :directive
			option = @supported_options[directive.name]
			if option && (the_alias = option[:alias_for])
				directive.name = the_alias
			end
		end
	end

	# After merging, indices are still up to date.
	def merge_options(current, prev)
		prev.directives.each do |directive|
			next if !@mergeable_options[directive.name]
			# Some weirdness in Nginx prevents passenger_enabled
			# from being inherited into subcontexts.
			next if directive.name == 'passenger_enabled'
			current[directive.name] ||= directive
		end
	end

	def register_app(apps, context)
		return if context['passenger_enabled'] != 'on'
		app_root = context['passenger_app_root'] || File.dirname(context['root'])
		app_group_name = context['passenger_app_group_name'] || app_root

		options = (apps[app_group_name] ||= {})
		options["PASSENGER_APP_ROOT"] ||= app_root
		context.directives.each do |directive|
			if option = @supported_options[directive.name]
				header_name = option[:header] || option[:name].upcase
				options[header_name] ||= directive.value
			end
		end
	end
end
