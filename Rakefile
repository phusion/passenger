# kate: syntax ruby

#  Phusion Passenger - http://www.modrails.com/
#  Copyright (C) 2008, 2009  Phusion
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

$LOAD_PATH.unshift("#{File.dirname(__FILE__)}/lib")
$LOAD_PATH.unshift("#{File.dirname(__FILE__)}/misc")
require 'rubygems'
require 'pathname'
require 'rake/rdoctask'
require 'rake/gempackagetask'
require 'rake/extensions'
require 'rake/cplusplus'
require 'phusion_passenger/platform_info'
require 'phusion_passenger/constants'

verbose true

##### Configuration

PACKAGE_VERSION = PhusionPassenger::VERSION_STRING
OPTIMIZE = ["yes", "on", "true"].include?(ENV['OPTIMIZE'])

include PlatformInfo

CC  = "gcc"
CXX = "g++"
LIBEXT = PlatformInfo.library_extension
if OPTIMIZE
	OPTIMIZATION_FLAGS = "#{PlatformInfo.debugging_cflags} -O2 -DBOOST_DISABLE_ASSERTS"
else
	OPTIMIZATION_FLAGS = "#{PlatformInfo.debugging_cflags} -DPASSENGER_DEBUG -DBOOST_DISABLE_ASSERTS"
end

# Extra compiler flags that should always be passed to the C/C++ compiler.
# Should be included last in the command string, even after PlatformInfo.portability_cflags.
EXTRA_CXXFLAGS = "-Wall #{OPTIMIZATION_FLAGS}"

# Extra linker flags that should always be passed to the linker.
# Should be included last in the command string, even after PlatformInfo.portability_ldflags.
EXTRA_LDFLAGS  = ""


#### Default tasks

desc "Build everything"
task :default => [
	:native_support,
	:apache2,
	:nginx,
	'test/oxt/oxt_test_main',
	'test/CxxTests'
]

desc "Remove compiled files"
task :clean

desc "Remove all generated files"
task :clobber


##### Ruby C extension

task :native_support => "ext/phusion_passenger/native_support.#{LIBEXT}"

file 'ext/phusion_passenger/Makefile' => 'ext/phusion_passenger/extconf.rb' do
	sh "cd ext/phusion_passenger && #{RUBY} extconf.rb"
end

file "ext/phusion_passenger/native_support.#{LIBEXT}" => [
	'ext/phusion_passenger/Makefile',
	'ext/phusion_passenger/native_support.c'
] do
	sh "cd ext/phusion_passenger && make"
end

task :clean do
	sh "cd ext/phusion_passenger && make clean" if File.exist?('ext/phusion_passenger/Makefile')
	sh "rm -f ext/phusion_passenger/Makefile"
end


##### Boost and OXT static library

def define_libboost_oxt_task(namespace, output_dir, extra_compiler_flags = nil)
	output_file = "#{output_dir}.a"
	flags = "-Iext #{extra_compiler_flags} #{PlatformInfo.portability_cflags} #{EXTRA_CXXFLAGS}"
	
	# Define compilation targets for .cpp files in ext/boost/src/pthread.
	boost_object_files = []
	Dir['ext/boost/src/pthread/*.cpp'].each do |source_file|
		object_name = File.basename(source_file.sub(/\.cpp$/, '.o'))
		boost_output_dir  = "#{output_dir}/boost"
		object_file = "#{boost_output_dir}/#{object_name}"
		boost_object_files << object_file
		
		file object_file => source_file do
			sh "mkdir -p #{boost_output_dir}" if !File.directory?(boost_output_dir)
			compile_cxx(source_file, "#{flags} -o #{object_file}")
		end
	end
	
	# Define compilation targets for .cpp files in ext/oxt.
	oxt_object_files = []
	oxt_dependency_files = Dir["ext/oxt/*.hpp"] + Dir["ext/oxt/detail/*.hpp"]
	Dir['ext/oxt/*.cpp'].each do |source_file|
		object_name = File.basename(source_file.sub(/\.cpp$/, '.o'))
		oxt_output_dir  = "#{output_dir}/oxt"
		object_file = "#{oxt_output_dir}/#{object_name}"
		oxt_object_files << object_file
		
		file object_file => [source_file, *oxt_dependency_files] do
			sh "mkdir -p #{oxt_output_dir}" if !File.directory?(oxt_output_dir)
			compile_cxx(source_file, "#{flags} -o #{object_file}")
		end
	end
	
	file(output_file => boost_object_files + oxt_object_files) do
		sh "mkdir -p #{output_dir}/boost #{output_dir}/oxt"
		create_static_library(output_file,
			"#{output_dir}/boost/*.o " <<
			"#{output_dir}/oxt/*.o")
	end
	
	task "#{namespace}:clean" do
		sh "rm -rf #{output_file} #{output_dir}"
	end
	
	return output_file
end


##### Static library for Passenger source files that are shared between
##### the Apache module and the Nginx helper server.

def define_common_library_task(namespace, output_dir, extra_compiler_flags = nil)
	static_library = "#{output_dir}.a"
	
	# Define compilation targets for the object files in libpassenger_common.
	flags =  "-Iext -Iext/common #{extra_compiler_flags} "
	flags << "#{PlatformInfo.portability_cflags} #{EXTRA_CXXFLAGS}"
	common_object_files = []
	['Utils.cpp', 'Logging.cpp', 'SystemTime.cpp', 'CachedFileStat.cpp',
	 'Base64.cpp', 'AccountsDatabase.cpp'].each do |source_file|
		object_name = source_file.sub(/\.cpp$/, '.o')
		object_file = "#{output_dir}/#{object_name}"
		header_file = source_file.sub(/\.cpp$/, '.h')
		common_object_files << object_file
		
		file object_file => ["ext/common/#{source_file}", "ext/common/#{header_file}"] do
			sh "mkdir -p #{output_dir}" if !File.directory?(output_dir)
			compile_cxx("ext/common/#{source_file}", "#{flags} -o #{object_file}")
		end
	end
	
	file(static_library => common_object_files) do
		sh "mkdir -p #{output_dir}"
		create_static_library(static_library, "#{output_dir}/*.o")
	end
	
	task "#{namespace}:clean" do
		sh "rm -rf #{static_library} #{output_dir}"
	end
	
	return static_library
end


##### Apache 2 module

	APACHE2_MODULE_CXXFLAGS = "-Iext -Iext/common #{PlatformInfo.apache2_module_cflags} " <<
		"#{PlatformInfo.portability_cflags} #{EXTRA_CXXFLAGS}"
	APACHE2_MODULE_INPUT_FILES = {
		'ext/apache2/Configuration.o' => %w(
			ext/apache2/Configuration.cpp
			ext/apache2/Configuration.h),
		'ext/apache2/Bucket.o' => %w(
			ext/apache2/Bucket.cpp
			ext/apache2/Bucket.h),
		'ext/apache2/Hooks.o' => %w(
			ext/apache2/Hooks.cpp
			ext/apache2/Hooks.h
			ext/apache2/Configuration.h
			ext/apache2/Bucket.h
			ext/apache2/DirectoryMapper.h
			ext/common/ApplicationPool.h
			ext/common/ApplicationPoolServer.h
			ext/common/SpawnManager.h
			ext/common/Exceptions.h
			ext/common/Application.h
			ext/common/MessageChannel.h
			ext/common/PoolOptions.h
			ext/common/StringListCreator.h
			ext/common/Version.h
			ext/common/Timer.h
			ext/common/Utils.h)
	}
	APACHE2_MODULE_OBJECTS = APACHE2_MODULE_INPUT_FILES.keys
	
	APACHE2_HELPER_CXXFLAGS = "-Iext -Iext/common #{PlatformInfo.portability_cflags} #{EXTRA_CXXFLAGS}"
	
	APACHE2_MODULE_BOOST_OXT_LIBRARY = define_libboost_oxt_task("apache2",
		"ext/apache2/module_libboost_oxt",
		PlatformInfo.apache2_module_cflags)
	APACHE2_MODULE_COMMON_LIBRARY    = define_common_library_task("apache2",
		"ext/apache2/module_libpassenger_common",
		PlatformInfo.apache2_module_cflags)
	APACHE2_HELPER_BOOST_OXT_LIBRARY = define_libboost_oxt_task("apache2",
		"ext/apache2/helper_libboost_oxt")
	APACHE2_HELPER_COMMON_LIBRARY    = define_common_library_task("apache2",
		"ext/apache2/helper_libpassenger_common")
	
	
	desc "Build Apache 2 module"
	task :apache2 => ['ext/apache2/mod_passenger.so', 'ext/apache2/Watchdog',
		'ext/apache2/HelperServer', :native_support]
	
	mod_passenger_dependencies = [APACHE2_MODULE_COMMON_LIBRARY,
		APACHE2_MODULE_BOOST_OXT_LIBRARY,
		'ext/apache2/mod_passenger.o',
		APACHE2_MODULE_OBJECTS]
	file 'ext/apache2/mod_passenger.so' => mod_passenger_dependencies do
		PlatformInfo.apxs2.nil?      and raise "Could not find 'apxs' or 'apxs2'."
		PlatformInfo.apache2ctl.nil? and raise "Could not find 'apachectl' or 'apache2ctl'."
		PlatformInfo.httpd.nil?      and raise "Could not find the Apache web server binary."
		
		# apxs totally sucks. We couldn't get it working correctly
		# on MacOS X (it had various problems with building universal
		# binaries), so we decided to ditch it and build/install the
		# Apache module ourselves.
		#
		# Oh, and libtool sucks too. Do we even need it anymore in 2008?
		
		sources = APACHE2_MODULE_OBJECTS.join(' ')
		sources << ' ext/apache2/mod_passenger.o'
		
		linkflags =
			"#{PlatformInfo.apache2_module_cflags} " <<
			"#{PlatformInfo.portability_cflags} " <<
			"#{EXTRA_CXXFLAGS} " <<
			"#{APACHE2_MODULE_COMMON_LIBRARY} " <<
			"#{APACHE2_MODULE_BOOST_OXT_LIBRARY} " <<
			"#{PlatformInfo.apache2_module_ldflags} " <<
			"#{PlatformInfo.portability_ldflags} " <<
			"#{EXTRA_LDFLAGS} "
		
		create_shared_library('ext/apache2/mod_passenger.so',
			sources, linkflags)
	end

	file 'ext/apache2/mod_passenger.o' => ['ext/apache2/mod_passenger.c'] do
		compile_c('ext/apache2/mod_passenger.c',
			APACHE2_MODULE_CXXFLAGS +
			" -o ext/apache2/mod_passenger.o")
	end
	
	apache2_watchdog_dependencies = [
		'ext/apache2/Watchdog.cpp',
		'ext/common/ServerInstanceDir.h',
		APACHE2_HELPER_COMMON_LIBRARY,
		APACHE2_HELPER_BOOST_OXT_LIBRARY]
	file 'ext/apache2/Watchdog' => apache2_watchdog_dependencies do
		create_executable('ext/apache2/Watchdog',
			'ext/apache2/Watchdog.cpp',
			"#{APACHE2_HELPER_CXXFLAGS} " <<
			"#{APACHE2_HELPER_COMMON_LIBRARY} " <<
			"#{APACHE2_HELPER_BOOST_OXT_LIBRARY} " <<
			"#{PlatformInfo.portability_ldflags} " <<
			"#{EXTRA_LDFLAGS}")
	end
	
	apache2_helper_server_dependencies = [
		'ext/apache2/HelperServer.cpp',
		'ext/common/ServerInstanceDir.h',
		'ext/common/MessageServer.h',
		'ext/common/Timer.h',
		APACHE2_HELPER_COMMON_LIBRARY,
		APACHE2_HELPER_BOOST_OXT_LIBRARY]
	file 'ext/apache2/HelperServer' => apache2_helper_server_dependencies do
		create_executable('ext/apache2/HelperServer',
			'ext/apache2/HelperServer.cpp',
			"#{APACHE2_HELPER_CXXFLAGS} " <<
			"#{APACHE2_HELPER_COMMON_LIBRARY} " <<
			"#{APACHE2_HELPER_BOOST_OXT_LIBRARY} " <<
			"#{PlatformInfo.portability_ldflags} " <<
			"#{EXTRA_LDFLAGS}")
	end

	# Define rules for the individual Apache 2 module source files.
	APACHE2_MODULE_INPUT_FILES.each_pair do |target, sources|
		file(target => sources) do
			object_basename = File.basename(target)
			compile_cxx(sources[0],
				APACHE2_MODULE_CXXFLAGS +
				" -o ext/apache2/#{object_basename}")
		end
	end

	task :clean => 'apache2:clean'
	desc "Clean all compiled Apache 2 files"
	task 'apache2:clean' do
		files = [APACHE2_MODULE_OBJECTS, %w(ext/apache2/mod_passenger.o
			ext/apache2/mod_passenger.so ext/apache2/Watchdog
			ext/apache2/HelperServer)]
		sh("rm", "-rf", *files.flatten)
	end


##### Nginx helper server

	NGINX_BOOST_OXT_LIBRARY = define_libboost_oxt_task("nginx", "ext/nginx/libboost_oxt")
	NGINX_COMMON_LIBRARY    = define_common_library_task("nginx", "ext/nginx/libpassenger_common")
	
	desc "Build Nginx helper server"
	task :nginx => ['ext/nginx/HelperServer', :native_support]
	
	helper_server_dependencies = [
		NGINX_BOOST_OXT_LIBRARY,
		NGINX_COMMON_LIBRARY,
		'ext/nginx/HelperServer.cpp',
		'ext/nginx/ScgiRequestParser.h',
		'ext/nginx/HttpStatusExtractor.h',
		'ext/common/StaticString.h',
		'ext/common/Account.h',
		'ext/common/AccountsDatabase.h',
		'ext/common/MessageServer.h',
		'ext/common/FileDescriptor.h',
		'ext/common/BacktracesServer.h',
		'ext/common/ApplicationPool/Interface.h',
		'ext/common/ApplicationPool/Pool.h'
		]
	file 'ext/nginx/HelperServer' => helper_server_dependencies do
		create_executable "ext/nginx/HelperServer",
			'ext/nginx/HelperServer.cpp',
			"-Iext -Iext/common " <<
			"#{PlatformInfo.portability_cflags} " <<
			"#{EXTRA_CXXFLAGS}  " <<
			"#{NGINX_COMMON_LIBRARY} " <<
			"#{NGINX_BOOST_OXT_LIBRARY} " <<
			"#{PlatformInfo.portability_ldflags} " <<
			"#{EXTRA_LDFLAGS}"
	end
	
	task :clean => 'nginx:clean'
	desc "Clean all compiled Nginx files"
	task 'nginx:clean' do
		sh("rm", "-rf", "ext/nginx/HelperServer")
	end


##### Unit tests

	TEST_BOOST_OXT_LIBRARY = define_libboost_oxt_task("test", "test/libboost_oxt")
	TEST_COMMON_LIBRARY    = define_common_library_task("test", "test/libpassenger_common")
	
	TEST_COMMON_CFLAGS = "-DTESTING_APPLICATION_POOL " <<
		"#{PlatformInfo.portability_cflags} #{EXTRA_CXXFLAGS}"
	
	desc "Run all unit tests and integration tests"
	task :test => ['test:oxt', 'test:cxx', 'test:ruby', 'test:integration']
	
	task :clean => 'test:clean'
	desc "Clean all compiled test files"
	task 'test:clean' do
		sh("rm -rf test/oxt/oxt_test_main test/oxt/*.o test/CxxTests test/*.o")
	end
	
	
	### OXT tests ###
	
	TEST_OXT_CFLAGS = "-I../../ext -I../support #{TEST_COMMON_CFLAGS}"
	TEST_OXT_LDFLAGS = "#{TEST_BOOST_OXT_LIBRARY} #{PlatformInfo.portability_ldflags} #{EXTRA_LDFLAGS}"
	TEST_OXT_OBJECTS = {
		'oxt_test_main.o' => %w(oxt_test_main.cpp),
		'backtrace_test.o' => %w(backtrace_test.cpp counter.hpp),
		'spin_lock_test.o' => %w(spin_lock_test.cpp),
		'dynamic_thread_group_test.o' => %w(dynamic_thread_group_test.cpp counter.hpp),
		'syscall_interruption_test.o' => %w(syscall_interruption_test.cpp)
	}
	
	TEST_CXX_CFLAGS = "-Iext -Iext/common -Iext/nginx -Itest/support " <<
		"#{PlatformInfo.apr_flags} #{PlatformInfo.apu_flags} #{TEST_COMMON_CFLAGS}"
	TEST_CXX_LDFLAGS = "#{PlatformInfo.apr_libs} #{PlatformInfo.apu_libs} " <<
		"#{TEST_COMMON_LIBRARY} #{TEST_BOOST_OXT_LIBRARY} " <<
		"#{PlatformInfo.portability_ldflags} #{EXTRA_LDFLAGS}"
	TEST_CXX_OBJECTS = {
		'test/CxxTestMain.o' => %w(
			test/CxxTestMain.cpp),
		'test/support/Support.o' => %w(
			test/support/Support.cpp
			test/support/Support.h),
		'test/MessageChannelTest.o' => %w(
			test/MessageChannelTest.cpp
			ext/common/MessageChannel.h
			ext/common/Exceptions.h
			ext/common/Timer.h
			ext/common/Utils.h),
		'test/SpawnManagerTest.o' => %w(
			test/SpawnManagerTest.cpp
			ext/common/SpawnManager.h
			ext/common/AbstractSpawnManager.h
			ext/common/PoolOptions.h
			ext/common/StringListCreator.h
			ext/common/Application.h
			ext/common/MessageChannel.h),
		'test/ApplicationPool_ServerTest.o' => %w(
			test/ApplicationPool_ServerTest.cpp
			ext/common/ApplicationPool/Interface.h
			ext/common/ApplicationPool/Server.h
			ext/common/ApplicationPool/Client.h
			ext/common/ApplicationPool/Pool.h
			ext/common/Account.h
			ext/common/AccountsDatabase.h
			ext/common/MessageServer.h
			ext/common/PoolOptions.h
			ext/common/StringListCreator.h
			ext/common/MessageChannel.h),
		'test/ApplicationPool_Server_PoolTest.o' => %w(
			test/ApplicationPool_Server_PoolTest.cpp
			test/ApplicationPool_PoolTestCases.cpp
			ext/common/ApplicationPool/Interface.h
			ext/common/ApplicationPool/Server.h
			ext/common/ApplicationPool/Client.h
			ext/common/ApplicationPool/Pool.h
			ext/common/AbstractSpawnManager.h
			ext/common/Account.h
			ext/common/AccountsDatabase.h
			ext/common/MessageServer.h
			ext/common/SpawnManager.h
			ext/common/PoolOptions.h
			ext/common/StringListCreator.h
			ext/common/Application.h
			ext/common/MessageChannel.h),
		'test/ApplicationPool_PoolTest.o' => %w(
			test/ApplicationPool_PoolTest.cpp
			test/ApplicationPool_PoolTestCases.cpp
			ext/common/ApplicationPool/Interface.h
			ext/common/ApplicationPool/Pool.h
			ext/common/AbstractSpawnManager.h
			ext/common/SpawnManager.h
			ext/common/PoolOptions.h
			ext/common/StringListCreator.h
			ext/common/FileChangeChecker.h
			ext/common/CachedFileStat.hpp
			ext/common/Application.h),
		'test/PoolOptionsTest.o' => %w(
			test/PoolOptionsTest.cpp
			ext/common/PoolOptions.h
			ext/common/StringListCreator.h),
		'test/StaticStringTest.o' => %w(
			test/StaticStringTest.cpp
			ext/common/StaticString.h),
		'test/Base64Test.o' => %w(
			test/Base64Test.cpp
			ext/common/Base64.h
			ext/common/Base64.cpp),
		'test/ScgiRequestParserTest.o' => %w(
			test/ScgiRequestParserTest.cpp
			ext/nginx/ScgiRequestParser.h
			ext/common/StaticString.h),
		'test/HttpStatusExtractorTest.o' => %w(
			test/HttpStatusExtractorTest.cpp
			ext/nginx/HttpStatusExtractor.h),
		'test/MessageServerTest.o' => %w(
			test/MessageServerTest.cpp
			ext/common/ApplicationPool/Client.h
			ext/common/Account.h
			ext/common/AccountsDatabase.h
			ext/common/MessageServer.h
			ext/common/MessageChannel.h),
		'test/ServerInstanceDir.o' => %w(
			test/ServerInstanceDirTest.cpp
			ext/common/ServerInstanceDir.h
			ext/common/Utils.h),
		'test/FileChangeCheckerTest.o' => %w(
			test/FileChangeCheckerTest.cpp
			ext/common/FileChangeChecker.h
			ext/common/CachedFileStat.hpp),
		'test/FileDescriptorTest.o' => %w(
			test/FileDescriptorTest.cpp
			ext/common/FileDescriptor.h),
		'test/SystemTimeTest.o' => %w(
			test/SystemTimeTest.cpp
			ext/common/SystemTime.h
			ext/common/SystemTime.cpp),
		'test/CachedFileStatTest.o' => %w(
			test/CachedFileStatTest.cpp
			ext/common/CachedFileStat.hpp
			ext/common/CachedFileStat.cpp),
		'test/UtilsTest.o' => %w(
			test/UtilsTest.cpp
			ext/common/Utils.h)
	}
	
	desc "Run unit tests for the OXT library"
	task 'test:oxt' => 'test/oxt/oxt_test_main' do
		sh "cd test && ./oxt/oxt_test_main"
	end
	
	# Define task for test/oxt/oxt_test_main.
	oxt_test_main_dependencies = TEST_OXT_OBJECTS.keys.map do |object|
		"test/oxt/#{object}"
	end
	oxt_test_main_dependencies << TEST_BOOST_OXT_LIBRARY
	file 'test/oxt/oxt_test_main' => oxt_test_main_dependencies do
		objects = TEST_OXT_OBJECTS.keys.map{ |x| "test/oxt/#{x}" }.join(' ')
		create_executable("test/oxt/oxt_test_main", objects, TEST_OXT_LDFLAGS)
	end
	
	# Define tasks for each OXT test source file.
	TEST_OXT_OBJECTS.each_pair do |target, sources|
		file "test/oxt/#{target}" => sources.map{ |x| "test/oxt/#{x}" } + ['test/support/Support.h'] do
			Dir.chdir('test/oxt') do
				puts "### In test/oxt:"
				compile_cxx sources[0], TEST_OXT_CFLAGS
			end
		end
	end
	
	
	### C++ components tests ###
	
	desc "Run unit tests for the Apache 2 and Nginx C++ components"
	task 'test:cxx' => ['test/CxxTests', :native_support] do
	        if ENV['GROUPS'].to_s.empty?
		        sh "cd test && ./CxxTests"
	        else
	                args = ENV['GROUPS'].split(",").map{ |name| "-g #{name}" }
	                sh "cd test && ./CxxTests #{args.join(' ')}"
                end
	end
	
	cxx_tests_dependencies = [TEST_CXX_OBJECTS.keys,
		TEST_BOOST_OXT_LIBRARY, TEST_COMMON_LIBRARY]
	file 'test/CxxTests' => cxx_tests_dependencies.flatten do
		objects = TEST_CXX_OBJECTS.keys.join(' ')
		create_executable("test/CxxTests", objects, TEST_CXX_LDFLAGS)
	end
	
	TEST_CXX_OBJECTS.each_pair do |target, sources|
		file(target => sources) do
			compile_cxx sources[0], "-o #{target} #{TEST_CXX_CFLAGS}"
		end
	end
	
	
	### Ruby components tests ###
	
	desc "Run unit tests for the Ruby libraries"
	task 'test:ruby' => :native_support do
		if PlatformInfo.rspec.nil?
			abort "RSpec is not installed for Ruby interpreter '#{PlatformInfo::RUBY}'. Please install it."
		else
			Dir.chdir("test") do
				ruby "#{PlatformInfo.rspec} -c -f s ruby/*.rb ruby/*/*.rb"
			end
		end
	end
	
	desc "Run coverage tests for the Ruby libraries"
	task 'test:rcov' => :native_support do
		if PlatformInfo.rspec.nil?
			abort "RSpec is not installed for Ruby interpreter '#{PlatformInfo::RUBY}'. Please install it."
		else
			Dir.chdir("test") do
				sh "rcov", "--exclude",
					"lib\/spec,\/spec$,_spec\.rb$,support\/,platform_info,integration_tests",
					PlatformInfo.rspec, "--", "-c", "-f", "s",
					*Dir["ruby/*.rb", "ruby/*/*.rb", "integration_tests.rb"]
			end
		end
	end
	
	
	### Integration tests ###
	
	desc "Run all integration tests"
	task 'test:integration' => ['test:integration:apache2', 'test:integration:nginx'] do
	end
	
	desc "Run Apache 2 integration tests"
	task 'test:integration:apache2' => [:apache2, :native_support] do
		if PlatformInfo.rspec.nil?
			abort "RSpec is not installed for Ruby interpreter '#{PlatformInfo::RUBY}'. Please install it."
		else
			Dir.chdir("test") do
				ruby "#{PlatformInfo.rspec} -c -f s integration_tests/apache2_tests.rb"
			end
		end
	end
	
	desc "Run Nginx integration tests"
	task 'test:integration:nginx' => :nginx do
		if PlatformInfo.rspec.nil?
			abort "RSpec is not installed for Ruby interpreter '#{PlatformInfo::RUBY}'. Please install it."
		else
			Dir.chdir("test") do
				ruby "#{PlatformInfo.rspec} -c -f s integration_tests/nginx_tests.rb"
			end
		end
	end
	
	desc "Run the 'restart' integration test infinitely, and abort if/when it fails"
	task 'test:restart' => [:apache2, :native_support] do
		Dir.chdir("test") do
			color_code_start = "\e[33m\e[44m\e[1m"
			color_code_end = "\e[0m"
			i = 1
			while true do
				puts "#{color_code_start}Test run #{i} (press Ctrl-C multiple times to abort)#{color_code_end}"
				sh "spec -c -f s integration_tests/apache2.rb -e 'mod_passenger running in Apache 2 : MyCook(tm) beta running on root URI should support restarting via restart.txt'"
				i += 1
			end
		end
	end


##### Documentation

subdir 'doc' do
	ASCIIDOC = 'asciidoc'
	ASCIIDOC_FLAGS = "-a toc -a numbered -a toclevels=3 -a icons"
	ASCII_DOCS = ['Security of user switching support',
		'Users guide Apache', 'Users guide Nginx',
		'Architectural overview']

	DOXYGEN = 'doxygen'
	
	desc "Generate all documentation"
	task :doc => [:rdoc]
	
	if PlatformInfo.find_command(DOXYGEN)
		task :doc => :doxygen
	end

	task :doc => ASCII_DOCS.map{ |x| "#{x}.html" }

	ASCII_DOCS.each do |name|
		file "#{name}.html" => ["#{name}.txt"] do
			if PlatformInfo.find_command(ASCIIDOC)
		  		sh "#{ASCIIDOC} #{ASCIIDOC_FLAGS} '#{name}.txt'"
			else
				sh "echo 'asciidoc required to build docs' > '#{name}.html'"
			end
		end
	end
	
	task :clobber => [:'doxygen:clobber'] do
		sh "rm -f *.html"
	end
	
	desc "Generate Doxygen C++ API documentation if necessary"
	task :doxygen => ['cxxapi']
	file 'cxxapi' => Dir['../ext/apache2/*.{h,c,cpp}'] do
		sh "doxygen"
	end

	desc "Force generation of Doxygen C++ API documentation"
	task :'doxygen:force' do
		sh "doxygen"
	end

	desc "Remove generated Doxygen C++ API documentation"
	task :'doxygen:clobber' do
		sh "rm -rf cxxapi"
	end
end

Rake::RDocTask.new(:clobber_rdoc => "rdoc:clobber", :rerdoc => "rdoc:force") do |rd|
	rd.main = "README"
	rd.rdoc_dir = "doc/rdoc"
	rd.rdoc_files.include("README", "DEVELOPERS.TXT",
		"lib/phusion_passenger/*.rb",
		"lib/phusion_passenger/*/*.rb",
		"misc/rake/extensions.rb",
		"ext/phusion_passenger/*.c")
	rd.template = "./doc/template/horo"
	rd.title = "Passenger Ruby API"
	rd.options << "-S" << "-N" << "-p" << "-H"
end


##### Packaging

spec = Gem::Specification.new do |s|
	s.platform = Gem::Platform::RUBY
	s.homepage = "http://www.modrails.com/"
	s.summary = "Apache module for Ruby on Rails support."
	s.name = "passenger"
	s.version = PACKAGE_VERSION
	s.rubyforge_project = "passenger"
	s.author = "Phusion - http://www.phusion.nl/"
	s.email = "info@phusion.nl"
	s.requirements << "fastthread" << "Apache 2 with development headers"
	s.require_paths = ["lib", "ext"]
	s.add_dependency 'rake', '>= 0.8.1'
	s.add_dependency 'fastthread', '>= 1.0.1'
	s.extensions << 'ext/phusion_passenger/extconf.rb'
	s.files = FileList[
		'Rakefile',
		'README',
		'DEVELOPERS.TXT',
		'LICENSE',
		'INSTALL',
		'NEWS',
		'lib/**/*.rb',
		'lib/**/*.py',
		'lib/phusion_passenger/templates/*',
		'lib/phusion_passenger/templates/apache2/*',
		'lib/phusion_passenger/templates/nginx/*',
		'bin/*',
		'doc/*',
		
		# If you're running 'rake package' for the first time, then these
		# files don't exist yet, and so won't be matched by the above glob.
		# So we add these filenames manually.
		'doc/Users guide Apache.html',
		'doc/Users guide Nginx.html',
		'doc/Security of user switching support.html',
		
		'doc/*/*',
		'doc/*/*/*',
		'doc/*/*/*/*',
		'doc/*/*/*/*/*',
		'doc/*/*/*/*/*/*',
		'man/*',
		'debian/*',
		'ext/common/*.{cpp,c,h,hpp}',
		'ext/apache2/*.{cpp,h,c,TXT}',
		'ext/nginx/*.{c,cpp,h}',
		'ext/nginx/config',
		'ext/boost/*.{hpp,TXT}',
		'ext/boost/**/*.{hpp,cpp,pl,inl,ipp}',
		'ext/oxt/*.hpp',
		'ext/oxt/*.cpp',
		'ext/oxt/detail/*.hpp',
		'ext/phusion_passenger/*.{c,rb}',
		'benchmark/*.{cpp,rb}',
		'misc/*',
		'misc/*/*',
		'vendor/**/*',
		'test/*.{rb,cpp,example}',
		'test/support/*.{cpp,h,rb}',
		'test/oxt/*.cpp',
		'test/ruby/**/*',
		'test/integration_tests/**/*',
		'test/stub/**/*'
	]
	s.executables = [
		'passenger-spawn-server',
		'passenger-install-apache2-module',
		'passenger-install-nginx-module',
		'passenger-config',
		'passenger-memory-stats',
		'passenger-make-enterprisey',
		'passenger-status',
		'passenger-stress-test'
	]
	s.has_rdoc = true
	s.extra_rdoc_files = ['README']
	s.rdoc_options <<
		"-S" << "-N" << "-p" << "-H" <<
		'--main' << 'README' <<
		'--title' << 'Passenger Ruby API'
	s.description = "Passenger is an Apache module for Ruby on Rails support."
end

Rake::GemPackageTask.new(spec) do |pkg|
	pkg.need_tar_gz = true
end

Rake::Task['package'].prerequisites.unshift(:doc)
Rake::Task['package:gem'].prerequisites.unshift(:doc)
Rake::Task['package:force'].prerequisites.unshift(:doc)
task :clobber => :'package:clean'

desc "Create a fakeroot, useful for building native packages"
task :fakeroot => [:apache2, :native_support, :doc] do
	require 'rbconfig'
	include Config
	fakeroot = "pkg/fakeroot"

	# We don't use CONFIG['archdir'] and the like because we want
	# the files to be installed to /usr, and the Ruby interpreter
	# on the packaging machine might be in /usr/local.
	libdir = "#{fakeroot}/usr/lib/ruby/#{CONFIG['ruby_version']}"
	extdir = "#{libdir}/#{CONFIG['arch']}"
	bindir = "#{fakeroot}/usr/bin"
	docdir = "#{fakeroot}/usr/share/doc/phusion_passenger"
	libexecdir = "#{fakeroot}/usr/lib/phusion_passenger"
	
	sh "rm -rf #{fakeroot}"
	sh "mkdir -p #{fakeroot}"
	
	sh "mkdir -p #{libdir}"
	sh "cp -R lib/phusion_passenger #{libdir}/"

	sh "mkdir -p #{extdir}/phusion_passenger"
	sh "cp -R ext/phusion_passenger/*.#{LIBEXT} #{extdir}/phusion_passenger/"
	
	sh "mkdir -p #{bindir}"
	sh "cp bin/* #{bindir}/"
	
	sh "mkdir -p #{libexecdir}"
	sh "cp ext/apache2/mod_passenger.so #{libexecdir}/"
	sh "mv #{fakeroot}/usr/bin/passenger-spawn-server #{libexecdir}/"
	sh "cp ext/apache2/ApplicationPoolServerExecutable #{libexecdir}/"
	
	sh "mkdir -p #{docdir}"
	sh "cp -R doc/* #{docdir}/"
	sh "rm", "-rf", *Dir["#{docdir}/{definitions.h,Doxyfile,template}"]
end

desc "Create a Debian package"
task 'package:debian' => :fakeroot do
	if Process.euid != 0
		STDERR.puts
		STDERR.puts "*** ERROR: the 'package:debian' task must be run as root."
		STDERR.puts
		exit 1
	end

	fakeroot = "pkg/fakeroot"
	raw_arch = `uname -m`.strip
	arch = case raw_arch
	when /^i.86$/
		"i386"
	when /^x86_64/
		"amd64"
	else
		raw_arch
	end
	
	sh "sed -i 's/Version: .*/Version: #{PACKAGE_VERSION}/' debian/control"
	sh "cp -R debian #{fakeroot}/DEBIAN"
	sh "sed -i 's/: any/: #{arch}/' #{fakeroot}/DEBIAN/control"
	sh "chown -R root:root #{fakeroot}"
	sh "dpkg -b #{fakeroot} pkg/passenger_#{PACKAGE_VERSION}-#{arch}.deb"
end


##### Misc

desc "Run 'sloccount' to see how much code Passenger has"
task :sloccount do
	ENV['LC_ALL'] = 'C'
	begin
		# sloccount doesn't recognize the scripts in
		# bin/ as Ruby, so we make symlinks with proper
		# extensions.
		tmpdir = ".sloccount"
		system "rm -rf #{tmpdir}"
		mkdir tmpdir
		Dir['bin/*'].each do |file|
			safe_ln file, "#{tmpdir}/#{File.basename(file)}.rb"
		end
		sh "sloccount", *Dir[
			"#{tmpdir}/*",
			"lib/phusion_passenger",
			"lib/rake/{cplusplus,extensions}.rb",
			"ext/apache2",
			"ext/nginx",
			"ext/common",
			"ext/oxt",
			"ext/phusion_passenger/*.c",
			"test/**/*.{cpp,rb}",
			"benchmark/*.{cpp,rb}"
		]
	ensure
		system "rm -rf #{tmpdir}"
	end
end

desc "Convert the NEWS items for the latest release to HTML"
task :news_as_html do
	# The text is in the following format:
	#
	#   Release x.x.x
	#   -------------
	#
	#    * Text.
	#    * More text.
	# * A header.
	#      With yet more text.
	#   
	#   Release y.y.y
	#   -------------
	#   .....
	require 'cgi'
	contents = File.read("NEWS")
	
	# We're only interested in the latest release, so extract the text for that.
	contents =~ /\A(Release.*?)^(Release|Older releases)/m
	
	# Now split the text into individual items.
	items = $1.split(/^ \*/)
	items.shift  # Delete the 'Release x.x.x' header.
	
	puts "<dl>"
	items.each do |item|
		item.strip!
		
		# Does this item have a header? It does if it consists of multiple lines, and
		# the next line is capitalized.
		lines = item.split("\n")
		if lines.size > 1 && lines[1].strip[0..0] == lines[1].strip[0..0].upcase
			puts "<dt>#{lines[0]}</dt>"
			lines.shift
			item = lines.join("\n")
			item.strip!
		end
		
		# Split into paragraphs. Empty lines are paragraph dividers.
		paragraphs = item.split(/^ *$/m)
		
		def format_paragraph(text)
			# Get rid of newlines: convert them into spaces.
			text.gsub!("\n", ' ')
			while text.index('  ')
				text.gsub!('  ', ' ')
			end
			
			# Auto-link to issue tracker.
			text.gsub!(/(bug|issue) #(\d+)/i) do
				url = "http://code.google.com/p/phusion-passenger/issues/detail?id=#{$2}"
				%Q(<{a href="#{url}"}>#{$1} ##{$2}<{/a}>)
			end
			
			text.strip!
			text = CGI.escapeHTML(text)
			text.gsub!(%r(&lt;\{(.*?)\}&gt;(.*?)&lt;\{/(.*?)\}&gt;)) do
				"<#{CGI.unescapeHTML $1}>#{$2}</#{CGI.unescapeHTML $3}>"
			end
			text
		end
		
		if paragraphs.size > 1
			STDOUT.write("<dd>")
			paragraphs.each do |paragraph|
				paragraph.gsub!(/\A\n+/, '')
				paragraph.gsub!(/\n+\Z/, '')
				
				if (paragraph =~ /\A       /)
					# Looks like a code block.
					paragraph.gsub!(/^       /m, '')
					puts "<pre lang=\"ruby\">#{CGI.escapeHTML(paragraph)}</pre>"
				else
					puts "<p>#{format_paragraph(paragraph)}</p>"
				end
			end
			STDOUT.write("</dd>\n")
		else
			puts "<dd>#{format_paragraph(item)}</dd>"
		end
	end
	puts "</dl>"
end
