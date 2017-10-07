#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2010-2017 Phusion Holding B.V.
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

if !PlatformInfo.passenger_needs_ruby_dev_header?
  NATIVE_SUPPORT_TARGET = nil

  task :native_support do
    # Do nothing.
  end

  task 'native_support:clean' do
    # Do nothing.
  end
else
  output_dir  = RUBY_EXTENSION_OUTPUT_DIR
  output_name = "passenger_native_support.#{libext}"
  source_dir  = "src/ruby_native_extension"
  NATIVE_SUPPORT_TARGET = File.join(output_dir, output_name)

  task :native_support => NATIVE_SUPPORT_TARGET
  task :clean => 'native_support:clean'

  dependencies = [
    File.join(output_dir, "Makefile"),
    "#{source_dir}/passenger_native_support.c"
  ]
  file(NATIVE_SUPPORT_TARGET => dependencies) do
    sh "mkdir -p '#{output_dir}'" if !File.exist?(output_dir)
    sh "cd '#{output_dir}' && make"
  end

  file(File.join(output_dir, "Makefile") => "#{source_dir}/extconf.rb") do
    sh "mkdir -p '#{output_dir}'" if !File.exist?(output_dir)
    extconf_rb = File.expand_path("#{source_dir}/extconf.rb")
    sh "cd '#{output_dir}' && #{PlatformInfo.ruby_command} '#{extconf_rb}'"
  end

  task 'native_support:clean' do
    sh "rm -rf #{output_dir}"
  end
end
