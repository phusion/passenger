#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2010-2013 Phusion
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

auto_generated_sources = [
	'ext/nginx/ConfigurationCommands.c',
	'ext/nginx/CreateLocationConfig.c',
	'ext/nginx/MergeLocationConfig.c',
	'ext/nginx/CacheLocationConfig.c',
	'ext/nginx/ConfigurationFields.h'
]

desc "Build Nginx support files"
task :nginx => [
	:nginx_without_native_support,
	NATIVE_SUPPORT_TARGET
].compact

# Workaround for https://github.com/jimweirich/rake/issues/274
task :_nginx => :nginx

task :nginx_without_native_support => [
	auto_generated_sources,
	AGENT_OUTPUT_DIR + 'PassengerHelperAgent',
	AGENT_OUTPUT_DIR + 'PassengerWatchdog',
	AGENT_OUTPUT_DIR + 'PassengerLoggingAgent',
	AGENT_OUTPUT_DIR + 'SpawnPreparer',
	AGENT_OUTPUT_DIR + 'TempDirToucher',
	COMMON_LIBRARY.only(*NGINX_LIBS_SELECTOR).link_objects
].flatten

task :clean => 'nginx:clean'
desc "Clean all compiled Nginx files"
task 'nginx:clean' => 'common:clean' do
	# Nothing to clean at this time.
end

def create_nginx_auto_generated_source_task(source)
	dependencies = [
		"#{source}.erb",
		'lib/phusion_passenger/nginx/config_options.rb'
	]
	file(source => dependencies) do
		template = TemplateRenderer.new("#{source}.erb")
		template.render_to(source)
	end
end

auto_generated_sources.each do |source|
	create_nginx_auto_generated_source_task(source)
end
