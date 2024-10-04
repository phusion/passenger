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

require 'rubygems/version.rb'

TEST_BOOST_OXT_LIBRARY = LIBBOOST_OXT
TEST_COMMON_LIBRARY    = COMMON_LIBRARY
TEST_COMMON_CFLAGS     = "-DTESTING_APPLICATION_POOL"

desc "Run all unit tests and integration tests"
task :test => ['test:oxt', 'test:cxx', 'test:ruby', 'test:node', 'test:integration']

desc "Clean all compiled test files"
task 'test:clean' do
  sh("rm -rf #{TEST_OUTPUT_DIR}")
  sh("rm -f test/cxx/*.#{PlatformInfo.precompiled_header_extension} test/cxx/*.gch test/cxx/*.pch")
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

  bundle_args = []
  if deps_target = string_option('DEPS_TARGET')
    if bundler_too_new?
      sh "bundle config set --local path #{shesc deps_target}"
    else
      bundle_args.concat(["--path", shesc(deps_target)])
    end
  end

  npm_args = ENV['NPM_ARGS'].to_s

  if !PlatformInfo.locate_ruby_tool('bundle') || bundler_too_old?
    sh "#{gem_install} bundler"
  end

  if install_base_deps
    unless Gem::Version.new(RUBY_VERSION) >= Gem::Version.new('3.0.0') || RUBY_PLATFORM =~ /darwin/
      if bundler_too_new?
        sh "bundle config set --local without future"
      else
        bundle_args.concat(["--without", "future"])
      end
    end
  else
    if bundler_too_new?
      sh "bundle config set --local without 'base future'"
    else
      bundle_args.concat(["--without", "base", "future"])
    end
  end
  sh "bundle install #{bundle_args.join(' ')} #{ENV['BUNDLE_ARGS']}"

  if boolean_option('NODE_MODULES', default)
    sh "npm install #{npm_args}"
  end
end

def bundler_version
  `bundle --version` =~ /version (.+)/
  Gem::Version.new($1)
end

def bundler_too_old?
  Gem::Version.new(bundler_version) < Gem::Version.new("1.1.10")
end

def bundler_too_new?
  Gem::Version.new(bundler_version) >= Gem::Version.new("2.1.0")
end
