#  Phusion Passenger - http://www.modrails.com/
#  Copyright (C) 2010  Phusion
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; version 2 of the License.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License along
#  with this program; if not, write to the Free Software Foundation, Inc.,
#  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

output_dir  = RUBY_EXTENSION_OUTPUT_DIR
output_name = "passenger_native_support.#{LIBEXT}"
source_dir  = "ext/ruby"

task :native_support => "#{output_dir}/#{output_name}"
task :clean => 'native_support:clean'

dependencies = [
	"#{output_dir}/Makefile",
	"#{source_dir}/passenger_native_support.c"
]
file("#{output_dir}/#{output_name}" => dependencies) do
	sh "mkdir -p '#{output_dir}'" if !File.exist?(output_dir)
	sh "cd '#{output_dir}' && make"
end

file "#{output_dir}/Makefile" => "#{source_dir}/extconf.rb" do
	sh "mkdir -p '#{output_dir}'" if !File.exist?(output_dir)
	extconf_rb = File.expand_path("#{source_dir}/extconf.rb")
	sh "cd '#{output_dir}' && #{PlatformInfo.ruby_command} '#{extconf_rb}'"
end

task 'native_support:clean' do
	Dir["ext/ruby/*"].each do |entry|
		if File.exist?("#{entry}/Makefile")
			sh "rm -rf #{entry}"
		end
	end
end