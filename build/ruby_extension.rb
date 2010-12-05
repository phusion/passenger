#  Phusion Passenger - http://www.modrails.com/
#  Copyright (c) 2010 Phusion
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