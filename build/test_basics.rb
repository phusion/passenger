#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2010-2014 Phusion
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

TEST_BOOST_OXT_LIBRARY = LIBBOOST_OXT
TEST_COMMON_LIBRARY    = COMMON_LIBRARY

TEST_COMMON_CFLAGS = "-DTESTING_APPLICATION_POOL #{EXTRA_CXXFLAGS}"

desc "Run all unit tests and integration tests"
task :test => ['test:oxt', 'test:cxx', 'test:ruby', 'test:node', 'test:integration']

desc "Clean all compiled test files"
task 'test:clean' do
  sh("rm -rf test/oxt/oxt_test_main test/oxt/*.o test/cxx/*.dSYM test/cxx/CxxTestMain")
  sh("rm -f test/cxx/*.o test/cxx/*/*.o test/cxx/*.gch")
  sh("rm -f test/support/allocate_memory")
end

task :clean => 'test:clean'

file 'test/support/allocate_memory' => 'test/support/allocate_memory.c' do
  create_c_executable('test/support/allocate_memory', 'test/support/allocate_memory.c')
end

desc "Install developer dependencies"
task 'test:install_deps' do
  gem_install = PlatformInfo.gem_command + " install --no-rdoc --no-ri"
  gem_install = "#{PlatformInfo.ruby_sudo_command} #{gem_install}" if boolean_option('SUDO')
  default = boolean_option('DEVDEPS_DEFAULT', true)
  install_base_deps = boolean_option('BASE_DEPS', default)
  install_doctools  = boolean_option('DOCTOOLS', default)

  if deps_target = string_option('DEPS_TARGET')
    bundle_args = " --path #{deps_target}"
  end

  if !PlatformInfo.locate_ruby_tool('bundle')
    sh "#{gem_install} bundler"
  end

  if install_base_deps && install_doctools
    sh "bundle install #{bundle_args} --without="
  else
    if install_base_deps
      sh "bundle install #{bundle_args} --without doc"
    end
    if install_doctools
      sh "bundle install #{bundle_args} --without base"
    end
  end
  if boolean_option('RAILS_BUNDLES', default)
    sh "cd test/stub/rails3.0 && bundle install #{bundle_args}"
    sh "cd test/stub/rails3.1 && bundle install #{bundle_args}"
    sh "cd test/stub/rails3.2 && bundle install #{bundle_args}"
    if RUBY_VERSION >= '1.9'
      sh "cd test/stub/rails4.0 && bundle install #{bundle_args}"
      sh "cd test/stub/rails4.1 && bundle install #{bundle_args}"
    end
  end
  if boolean_option('NODE_MODULES', default)
    sh "npm install"
  end
end
