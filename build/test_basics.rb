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

TEST_BOOST_OXT_LIBRARY = LIBBOOST_OXT
TEST_COMMON_LIBRARY    = COMMON_LIBRARY
TEST_COMMON_CFLAGS     = "-DTESTING_APPLICATION_POOL"

desc "Run all unit tests and integration tests"
task :test => ['test:oxt', 'test:cxx', 'test:ruby', 'test:node', 'test:integration']

desc "Clean all compiled test files"
task 'test:clean' do
  sh("rm -rf #{TEST_OUTPUT_DIR}")
  sh("rm -f test/cxx/*.gch")
end

task :clean => 'test:clean'

file "#{TEST_OUTPUT_DIR}allocate_memory" => 'test/support/allocate_memory.c' do
  compile_c("#{TEST_OUTPUT_DIR}allocate_memory.o", 'test/support/allocate_memory.c')
  create_c_executable("#{TEST_OUTPUT_DIR}allocate_memory", "#{TEST_OUTPUT_DIR}allocate_memory.o")
end

desc "Install developer dependencies"
task 'test:install_deps' do
  gem_install = PlatformInfo.gem_command + " install --no-rdoc --no-ri"
  gem_install = "#{PlatformInfo.ruby_sudo_command} #{gem_install}" if boolean_option('SUDO')
  default = boolean_option('DEVDEPS_DEFAULT', true)
  install_base_deps = boolean_option('BASE_DEPS', default)

  if deps_target = string_option('DEPS_TARGET')
    bundle_args = "--path #{shesc deps_target} #{ENV['BUNDLE_ARGS']}".strip
  else
    bundle_args = ENV['BUNDLE_ARGS'].to_s
  end

  yarn_args = ENV['YARN_ARGS'].to_s

  if !PlatformInfo.locate_ruby_tool('bundle') || bundler_too_old?
    sh "#{gem_install} bundler"
  end

  if install_base_deps
    sh "bundle install #{bundle_args} --without="
  else
    sh "bundle install #{bundle_args} --without base"
  end

  if boolean_option('NODE_MODULES', default)
    sh "yarn install #{yarn_args}"
  end
end


def bundler_too_old?
  `bundle --version` =~ /version (.+)/
  version = $1.split('.').map { |x| x.to_i }
  version[0] < 1 || version[0] == 1 && version[1] < 10
end
