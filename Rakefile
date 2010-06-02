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

source_root = File.expand_path(File.dirname(__FILE__))
$LOAD_PATH.unshift("#{source_root}/lib")
$LOAD_PATH.unshift("#{source_root}/misc")
require 'rubygems'
require 'pathname'
require 'rake/rdoctask'
require 'rake/gempackagetask'
require 'rake/extensions'
require 'rake/cplusplus'
require 'phusion_passenger'
require 'phusion_passenger/packaging'
require 'phusion_passenger/platform_info'
require 'phusion_passenger/platform_info/operating_system'
require 'phusion_passenger/platform_info/binary_compatibility'
require 'phusion_passenger/platform_info/ruby'
require 'phusion_passenger/platform_info/apache'
require 'phusion_passenger/platform_info/compiler'
require 'phusion_passenger/platform_info/documentation_tools'
require "#{source_root}/config" if File.exist?("#{source_root}/config.rb")

verbose true unless ENV['REALLY_QUIET']
if ENV['STDERR_TO_STDOUT']
	# Just redirecting the file descriptor isn't enough because
	# data written to STDERR might arrive in an unexpected order
	# compared to STDOUT.
	STDERR.reopen(STDOUT)
	Object.send(:remove_const, :STDERR)
	STDERR = STDOUT
	$stderr = $stdout
end

##### Configuration

include PhusionPassenger
include PhusionPassenger::PlatformInfo

OPTIMIZE = ["yes", "on", "true"].include?(ENV['OPTIMIZE'])

CC  = PlatformInfo.env_defined?('CC')  ? ENV['CC']  : 'gcc'
CXX = PlatformInfo.env_defined?('CXX') ? ENV['CXX'] : 'g++'
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

# Whether to use the vendored libev or the system one.
USE_VENDORED_LIBEV = ["", "yes", "true", "1"].include?(ENV['USE_VENDORED_LIBEV'].to_s)

#### Default tasks

desc "Build everything"
task :default => [
	:native_support,
	:apache2,
	:nginx,
	'test/oxt/oxt_test_main',
	'test/cxx/CxxTestMain'
]

desc "Remove compiled files"
task :clean do
	sh "rm -rf .cache"
end

desc "Remove all generated files"
task :clobber


##### Ruby C extension

native_support_archdir = PlatformInfo.ruby_extension_binary_compatibility_ids.join("-")
task :native_support => "ext/phusion_passenger/#{native_support_archdir}/native_support.#{LIBEXT}"

file "ext/phusion_passenger/#{native_support_archdir}/Makefile" => 'ext/phusion_passenger/extconf.rb' do
	if !File.exist?("ext/phusion_passenger/#{native_support_archdir}")
		sh "mkdir -p ext/phusion_passenger/#{native_support_archdir}"
	end
	sh "cd ext/phusion_passenger/#{native_support_archdir} && #{RUBY} ../extconf.rb"
end

file "ext/phusion_passenger/#{native_support_archdir}/native_support.#{LIBEXT}" => [
	"ext/phusion_passenger/#{native_support_archdir}/Makefile",
	"ext/phusion_passenger/native_support.c"
] do
	if !File.exist?("ext/phusion_passenger/#{native_support_archdir}")
		sh "mkdir -p ext/phusion_passenger/#{native_support_archdir}"
	end
	sh "cd ext/phusion_passenger/#{native_support_archdir} && make"
end

task :clean => 'native_support:clean'

task 'native_support:clean' do
	Dir["ext/phusion_passenger/*"].each do |entry|
		if File.exist?("#{entry}/Makefile")
			sh "rm -rf #{entry}"
		end
	end
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


##### libev

if USE_VENDORED_LIBEV
	LIBEV_CFLAGS = "-Iext/libev"
	LIBEV_LIBS = "ext/libev/.libs/libev.a"
	
	task :libev => "ext/libev/.libs/libev.a"
	
	file "ext/libev/Makefile" => ["ext/libev/configure", "ext/libev/config.h.in", "ext/libev/Makefile.am"] do
		sh "cd ext/libev && sh ./configure --disable-shared --enable-static"
	end
	
	libev_sources = Dir["ext/libev/{*.c,*.h}"]
	file "ext/libev/.libs/libev.a" => ["ext/libev/Makefile"] + libev_sources do
		sh "rm -f ext/libev/libev.la"
		sh "cd ext/libev && make libev.la"
	end
	
	task :clean do
		if File.exist?("ext/libev/Makefile")
			sh "cd ext/libev && make maintainer-clean"
		end
	end
else
	LIBEV_CFLAGS = ""
	LIBEV_LIBS = ""
	task :libev
end


##### Static library for Passenger source files that are shared between
##### the Apache module and the Nginx helper agent.

def define_common_library_task(namespace, output_dir, extra_compiler_flags = nil)
	components = {
		'Logging.o' => %w(
			Logging.cpp
			Logging.h),
		'Utils/CachedFileStat.o' => %w(
			Utils/CachedFileStat.cpp
			Utils/CachedFileStat.h
			Utils/CachedFileStat.hpp),
		'Utils/Base64.o' => %w(
			Utils/Base64.cpp
			Utils/Base64.h),
		'Utils/MD5.o' => %w(
			Utils/MD5.cpp
			Utils/MD5.h),
		'Utils/SystemTime.o' => %w(
			Utils/SystemTime.cpp
			Utils/SystemTime.h),
		'Utils/StrIntUtils.o' => %w(
			Utils/StrIntUtils.cpp
			Utils/StrIntUtils.h),
		'Utils.o' => %w(
			Utils.cpp
			Utils.h
			Utils/Base64.h
			Utils/StrIntUtils.h
			ResourceLocator.h),
		'AccountsDatabase.o' => %w(
			AccountsDatabase.cpp
			AccountsDatabase.h
			RandomGenerator.h
			Constants.h
			Utils.h),
		'AgentsStarter.o' => %w(
			AgentsStarter.cpp
			AgentsStarter.h
			AgentsStarter.hpp
			ResourceLocator.h
			MessageClient.h
			MessageChannel.h
			ServerInstanceDir.h
			Utils/VariantMap.h),
		'AgentBase.o' => %w(
			AgentBase.cpp
			AgentBase.h
			Utils/VariantMap.h),
		'BCrypt.o' => %w(
			BCrypt.cpp
			BCrypt.h
			Blowfish.h
			Blowfish.c)
	}
	
	static_library = "#{output_dir}.a"
	
	# Define compilation targets for the object files in libpassenger_common.
	flags =  "-Iext -Iext/common #{LIBEV_CFLAGS} #{extra_compiler_flags} "
	flags << "#{PlatformInfo.portability_cflags} #{EXTRA_CXXFLAGS}"
	common_object_files = []
	components.each_pair do |object_name, dependencies|
		source_file = dependencies[0]
		object_file = "#{output_dir}/#{object_name}"
		common_object_files << object_file
		dependencies = dependencies.map do |dep|
			"ext/common/#{dep}"
		end
		
		file object_file => dependencies do
			sh "mkdir -p #{output_dir}" if !File.directory?(output_dir)
			sh "mkdir -p #{output_dir}/Utils" if !File.directory?("#{output_dir}/Utils")
			compile_cxx("ext/common/#{source_file}", "#{flags} -o #{object_file}")
		end
	end
	
	file(static_library => common_object_files) do
		create_static_library(static_library, "#{output_dir}/*.o #{output_dir}/Utils/*.o")
	end
	
	task "#{namespace}:clean" do
		sh "rm -rf #{static_library} #{output_dir}"
	end
	
	return static_library
end


##### Common stuff

	BOOST_OXT_LIBRARY = define_libboost_oxt_task("common",
		"ext/common/libboost_oxt")
	COMMON_LIBRARY    = define_common_library_task("common",
		"ext/common/libpassenger_common")
	
	task :clean => 'common:clean' do
		sh "rm -f agents/PassengerWatchdog agents/PassengerLoggingAgent"
	end
	
	watchdog_dependencies = [
		'ext/common/Watchdog.cpp',
		'ext/common/ServerInstanceDir.h',
		'ext/common/ResourceLocator.h',
		'ext/common/Utils/VariantMap.h',
		COMMON_LIBRARY,
		BOOST_OXT_LIBRARY]
	file 'agents/PassengerWatchdog' => watchdog_dependencies do
		sh "mkdir -p agents" if !File.directory?("agents")
		create_executable('agents/PassengerWatchdog',
			'ext/common/Watchdog.cpp',
			"-Iext -Iext/common #{PlatformInfo.portability_cflags} #{EXTRA_CXXFLAGS} " <<
			"#{COMMON_LIBRARY} " <<
			"#{BOOST_OXT_LIBRARY} " <<
			"#{PlatformInfo.portability_ldflags} " <<
			"#{EXTRA_LDFLAGS}")
	end
	
	logging_agent_dependencies = [
		'ext/common/LoggingAgent/Main.cpp',
		'ext/common/LoggingAgent/LoggingServer.h',
		'ext/common/ServerInstanceDir.h',
		'ext/common/Logging.h',
		'ext/common/Utils/VariantMap.h',
		COMMON_LIBRARY,
		BOOST_OXT_LIBRARY,
		:libev]
	file 'agents/PassengerLoggingAgent' => logging_agent_dependencies do
		sh "mkdir -p agents" if !File.directory?("agents")
		create_executable('agents/PassengerLoggingAgent',
			'ext/common/LoggingAgent/Main.cpp',
			"-Iext -Iext/common #{LIBEV_CFLAGS} #{PlatformInfo.portability_cflags} #{EXTRA_CXXFLAGS} " <<
			"#{COMMON_LIBRARY} " <<
			"#{BOOST_OXT_LIBRARY} " <<
			"#{LIBEV_LIBS} " <<
			"#{PlatformInfo.portability_ldflags} " <<
			"#{EXTRA_LDFLAGS}")
	end


##### Apache 2 module

	APACHE2_MODULE_CXXFLAGS = "-Iext -Iext/common #{PlatformInfo.apache2_module_cflags} " <<
		"#{PlatformInfo.portability_cflags} #{EXTRA_CXXFLAGS}"
	APACHE2_MODULE_INPUT_FILES = {
		'ext/apache2/Configuration.o' => %w(
			ext/apache2/Configuration.cpp
			ext/apache2/Configuration.h
			ext/apache2/Configuration.hpp
			ext/common/Constants.h),
		'ext/apache2/Bucket.o' => %w(
			ext/apache2/Bucket.cpp
			ext/apache2/Bucket.h),
		'ext/apache2/Hooks.o' => %w(
			ext/apache2/Hooks.cpp
			ext/apache2/Hooks.h
			ext/apache2/Configuration.h
			ext/apache2/Configuration.hpp
			ext/apache2/Bucket.h
			ext/apache2/DirectoryMapper.h
			ext/common/AgentsStarter.hpp
			ext/common/ApplicationPool/Client.h
			ext/common/SpawnManager.h
			ext/common/Exceptions.h
			ext/common/Process.h
			ext/common/Session.h
			ext/common/Logging.h
			ext/common/RandomGenerator.h
			ext/common/MessageChannel.h
			ext/common/ServerInstanceDir.h
			ext/common/PoolOptions.h
			ext/common/StringListCreator.h
			ext/common/Constants.h
			ext/common/Utils.h
			ext/common/Utils/Timer.h)
	}
	APACHE2_MODULE_OBJECTS = APACHE2_MODULE_INPUT_FILES.keys
	
	APACHE2_HELPER_CXXFLAGS = "-Iext -Iext/common #{PlatformInfo.portability_cflags} #{EXTRA_CXXFLAGS}"
	
	APACHE2_MODULE_BOOST_OXT_LIBRARY = define_libboost_oxt_task("apache2",
		"ext/apache2/module_libboost_oxt",
		PlatformInfo.apache2_module_cflags)
	APACHE2_MODULE_COMMON_LIBRARY    = define_common_library_task("apache2",
		"ext/apache2/module_libpassenger_common",
		PlatformInfo.apache2_module_cflags)
	APACHE2_HELPER_BOOST_OXT_LIBRARY = BOOST_OXT_LIBRARY
	APACHE2_HELPER_COMMON_LIBRARY    = COMMON_LIBRARY
	
	
	desc "Build Apache 2 module"
	task :apache2 => ['ext/apache2/mod_passenger.so',
		'agents/apache2/PassengerHelperAgent',
		'agents/PassengerWatchdog',
		'agents/PassengerLoggingAgent',
		:native_support]
	
	mod_passenger_dependencies = [APACHE2_MODULE_COMMON_LIBRARY,
		APACHE2_MODULE_BOOST_OXT_LIBRARY,
		'ext/apache2/mod_passenger.o',
		APACHE2_MODULE_OBJECTS].flatten
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
	
	apache2_helper_agent_dependencies = [
		'ext/apache2/HelperAgent.cpp',
		'ext/common/ServerInstanceDir.h',
		'ext/common/MessageServer.h',
		'ext/common/Logging.h',
		'ext/common/SpawnManager.h',
		'ext/common/Account.h',
		'ext/common/ResourceLocator.h',
		'ext/common/Utils.h',
		'ext/common/Utils/Timer.h',
		'ext/common/Utils/ProcessMetricsCollector.h',
		'ext/common/ApplicationPool/Interface.h',
		'ext/common/ApplicationPool/Pool.h',
		'ext/common/ApplicationPool/Server.h',
		APACHE2_HELPER_COMMON_LIBRARY,
		APACHE2_HELPER_BOOST_OXT_LIBRARY]
	file 'agents/apache2/PassengerHelperAgent' => apache2_helper_agent_dependencies do
		sh "mkdir -p agents/apache2" if !File.directory?("agents/apache2")
		create_executable('agents/apache2/PassengerHelperAgent',
			'ext/apache2/HelperAgent.cpp',
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
		files = [APACHE2_MODULE_OBJECTS]
		files += %w(
			ext/apache2/mod_passenger.o
			ext/apache2/mod_passenger.so
			agents/apache2/PassengerHelperAgent
		)
		sh("rm", "-rf", *files.flatten)
	end


##### Nginx helper agent

	NGINX_BOOST_OXT_LIBRARY = BOOST_OXT_LIBRARY
	NGINX_COMMON_LIBRARY    = COMMON_LIBRARY
	
	desc "Build Nginx helper agent"
	task :nginx => ['agents/nginx/PassengerHelperAgent',
			'agents/PassengerWatchdog',
			'agents/PassengerLoggingAgent',
			:native_support]
	
	helper_agent_dependencies = [
		NGINX_BOOST_OXT_LIBRARY,
		NGINX_COMMON_LIBRARY,
		'ext/nginx/HelperAgent.cpp',
		'ext/nginx/ScgiRequestParser.h',
		'ext/nginx/HttpStatusExtractor.h',
		'ext/common/StaticString.h',
		'ext/common/Account.h',
		'ext/common/AccountsDatabase.h',
		'ext/common/MessageServer.h',
		'ext/common/FileDescriptor.h',
		'ext/common/SpawnManager.h',
		'ext/common/Logging.h',
		'ext/common/ResourceLocator.h',
		'ext/common/Utils/ProcessMetricsCollector.h',
		'ext/common/Utils/VariantMap.h',
		'ext/common/HelperAgent/BacktracesServer.h',
		'ext/common/ApplicationPool/Interface.h',
		'ext/common/ApplicationPool/Pool.h',
		'ext/common/ApplicationPool/Server.h'
		]
	file 'agents/nginx/PassengerHelperAgent' => helper_agent_dependencies do
		sh "mkdir -p agents/nginx" if !File.directory?("agents/nginx")
		create_executable "agents/nginx/PassengerHelperAgent",
			'ext/nginx/HelperAgent.cpp',
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
		sh("rm", "-rf", "agents/nginx/PassengerHelperAgent")
	end


##### Unit tests

	TEST_BOOST_OXT_LIBRARY = BOOST_OXT_LIBRARY
	TEST_COMMON_LIBRARY    = COMMON_LIBRARY
	
	TEST_COMMON_CFLAGS = "-DTESTING_APPLICATION_POOL " <<
		"#{PlatformInfo.portability_cflags} #{EXTRA_CXXFLAGS}"
	
	desc "Run all unit tests and integration tests"
	task :test => ['test:oxt', 'test:cxx', 'test:ruby', 'test:integration']
	
	task :clean => 'test:clean'
	desc "Clean all compiled test files"
	task 'test:clean' do
		sh("rm -rf test/oxt/oxt_test_main test/oxt/*.o test/cxx/CxxTestMain test/cxx/*.o")
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
		file "test/oxt/#{target}" => sources.map{ |x| "test/oxt/#{x}" } do
			Dir.chdir('test/oxt') do
				puts "### In test/oxt:"
				compile_cxx sources[0], TEST_OXT_CFLAGS
			end
		end
	end
	
	
	### C++ components tests ###
	
	TEST_CXX_CFLAGS = "-Iext -Iext/common -Iext/nginx #{LIBEV_CFLAGS} -Itest/support " <<
		"#{TEST_COMMON_CFLAGS}"
	TEST_CXX_LDFLAGS = "#{TEST_COMMON_LIBRARY} #{TEST_BOOST_OXT_LIBRARY} #{LIBEV_LIBS} " <<
		"#{PlatformInfo.portability_ldflags} #{EXTRA_LDFLAGS}"
	TEST_CXX_OBJECTS = {
		'test/cxx/CxxTestMain.o' => %w(
			test/cxx/CxxTestMain.cpp),
		'test/cxx/TestSupport.o' => %w(
			test/cxx/TestSupport.cpp
			test/cxx/TestSupport.h),
		'test/cxx/MessageChannelTest.o' => %w(
			test/cxx/MessageChannelTest.cpp
			ext/common/MessageChannel.h
			ext/common/Exceptions.h
			ext/common/Utils.h
			ext/common/Utils/Timer.h),
		'test/cxx/MessageReadersWritersTest.o' => %w(
			test/cxx/MessageReadersWritersTest.cpp
			ext/common/MessageReadersWriters.h
			ext/common/Exceptions.h
			ext/common/StaticString.h
			ext/common/Utils/MemZeroGuard.h),
		'test/cxx/SpawnManagerTest.o' => %w(
			test/cxx/SpawnManagerTest.cpp
			ext/common/SpawnManager.h
			ext/common/AbstractSpawnManager.h
			ext/common/PoolOptions.h
			ext/common/Logging.h
			ext/common/StringListCreator.h
			ext/common/Process.h
			ext/common/AccountsDatabase.h
			ext/common/Account.h
			ext/common/Session.h
			ext/common/Constants.h
			ext/common/MessageChannel.h),
		'test/cxx/ApplicationPool_ServerTest.o' => %w(
			test/cxx/ApplicationPool_ServerTest.cpp
			ext/common/ApplicationPool/Interface.h
			ext/common/ApplicationPool/Server.h
			ext/common/ApplicationPool/Client.h
			ext/common/ApplicationPool/Pool.h
			ext/common/Account.h
			ext/common/AccountsDatabase.h
			ext/common/MessageServer.h
			ext/common/Session.h
			ext/common/PoolOptions.h
			ext/common/Logging.h
			ext/common/StringListCreator.h
			ext/common/MessageChannel.h
			ext/common/Utils/ProcessMetricsCollector.h),
		'test/cxx/ApplicationPool_Server_PoolTest.o' => %w(
			test/cxx/ApplicationPool_Server_PoolTest.cpp
			test/cxx/ApplicationPool_PoolTestCases.cpp
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
			ext/common/Logging.h
			ext/common/StringListCreator.h
			ext/common/Process.h
			ext/common/Session.h
			ext/common/MessageChannel.h
			ext/common/Utils/ProcessMetricsCollector.h),
		'test/cxx/ApplicationPool_PoolTest.o' => %w(
			test/cxx/ApplicationPool_PoolTest.cpp
			test/cxx/ApplicationPool_PoolTestCases.cpp
			ext/common/ApplicationPool/Interface.h
			ext/common/ApplicationPool/Pool.h
			ext/common/AbstractSpawnManager.h
			ext/common/SpawnManager.h
			ext/common/PoolOptions.h
			ext/common/Logging.h
			ext/common/StringListCreator.h
			ext/common/Utils/FileChangeChecker.h
			ext/common/Utils/CachedFileStat.hpp
			ext/common/Process.h
			ext/common/Session.h),
		'test/cxx/PoolOptionsTest.o' => %w(
			test/cxx/PoolOptionsTest.cpp
			ext/common/PoolOptions.h
			ext/common/Session.h
			ext/common/Logging.h
			ext/common/StringListCreator.h),
		'test/cxx/StaticStringTest.o' => %w(
			test/cxx/StaticStringTest.cpp
			ext/common/StaticString.h),
		'test/cxx/Base64Test.o' => %w(
			test/cxx/Base64Test.cpp
			ext/common/Utils/Base64.h
			ext/common/Utils/Base64.cpp),
		'test/cxx/ScgiRequestParserTest.o' => %w(
			test/cxx/ScgiRequestParserTest.cpp
			ext/nginx/ScgiRequestParser.h
			ext/common/StaticString.h),
		'test/cxx/HttpStatusExtractorTest.o' => %w(
			test/cxx/HttpStatusExtractorTest.cpp
			ext/nginx/HttpStatusExtractor.h),
		'test/cxx/LoggingTest.o' => %w(
			test/cxx/LoggingTest.cpp
			ext/common/LoggingAgent/LoggingServer.h
			ext/common/Logging.h
			ext/common/Utils.h
			ext/common/EventedServer.h
			ext/common/EventedMessageServer.h
			ext/common/MessageReadersWriters.h
			ext/common/MessageClient.h),
		'test/cxx/MessageServerTest.o' => %w(
			test/cxx/MessageServerTest.cpp
			ext/common/ApplicationPool/Client.h
			ext/common/ApplicationPool/Pool.h
			ext/common/PoolOptions.h
			ext/common/SpawnManager.h
			ext/common/Session.h
			ext/common/Logging.h
			ext/common/Account.h
			ext/common/AccountsDatabase.h
			ext/common/Session.h
			ext/common/MessageServer.h
			ext/common/MessageChannel.h),
		'test/cxx/ServerInstanceDir.o' => %w(
			test/cxx/ServerInstanceDirTest.cpp
			ext/common/ServerInstanceDir.h
			ext/common/Utils.h),
		'test/cxx/FileChangeCheckerTest.o' => %w(
			test/cxx/FileChangeCheckerTest.cpp
			ext/common/Utils/FileChangeChecker.h
			ext/common/Utils/CachedFileStat.hpp),
		'test/cxx/FileDescriptorTest.o' => %w(
			test/cxx/FileDescriptorTest.cpp
			ext/common/FileDescriptor.h),
		'test/cxx/SystemTimeTest.o' => %w(
			test/cxx/SystemTimeTest.cpp
			ext/common/Utils/SystemTime.h
			ext/common/Utils/SystemTime.cpp),
		'test/cxx/CachedFileStatTest.o' => %w(
			test/cxx/CachedFileStatTest.cpp
			ext/common/Utils/CachedFileStat.hpp
			ext/common/Utils/CachedFileStat.cpp),
		'test/cxx/VariantMapTest.o' => %w(
			test/cxx/VariantMapTest.cpp
			ext/common/MessageChannel.h
			ext/common/Utils/VariantMap.h),
		'test/cxx/UtilsTest.o' => %w(
			test/cxx/UtilsTest.cpp
			ext/common/Utils.h)
	}
	
	desc "Run unit tests for the Apache 2 and Nginx C++ components"
	task 'test:cxx' => ['test/cxx/CxxTestMain', :native_support] do
	        if ENV['GROUPS'].to_s.empty?
		        sh "cd test && ./cxx/CxxTestMain"
	        else
	                args = ENV['GROUPS'].split(",").map{ |name| "-g #{name}" }
	                sh "cd test && ./cxx/CxxTestMain #{args.join(' ')}"
                end
	end
	
	cxx_tests_dependencies = [TEST_CXX_OBJECTS.keys, :libev,
		TEST_BOOST_OXT_LIBRARY, TEST_COMMON_LIBRARY]
	file 'test/cxx/CxxTestMain' => cxx_tests_dependencies.flatten do
		objects = TEST_CXX_OBJECTS.keys.join(' ')
		create_executable("test/cxx/CxxTestMain", objects, TEST_CXX_LDFLAGS)
	end
	
	TEST_CXX_OBJECTS.each_pair do |target, sources|
		file(target => sources + ['test/cxx/TestSupport.h']) do
			compile_cxx sources[0], "-o #{target} #{TEST_CXX_CFLAGS}"
		end
	end
	
	
	### Ruby components tests ###
	
	desc "Run unit tests for the Ruby libraries"
	task 'test:ruby' => [:native_support, 'agents/PassengerLoggingAgent'] do
		if PlatformInfo.rspec.nil?
			abort "RSpec is not installed for Ruby interpreter '#{PlatformInfo::RUBY}'. Please install it."
		else
			Dir.chdir("test") do
				ruby "#{PlatformInfo.rspec} -c -f s ruby/*_spec.rb ruby/*/*_spec.rb"
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

ASCIIDOC_FLAGS = "-a toc -a numbered -a toclevels=3 -a icons"
DOXYGEN = 'doxygen'

desc "Generate all documentation"
task :doc => [:rdoc]

if PlatformInfo.find_command(DOXYGEN)
	task :doc => :doxygen
end

task :doc => Packaging::ASCII_DOCS

Packaging::ASCII_DOCS.each do |target|
	source = target.sub(/\.html$/, '.txt')
	file target => [source] + Dir["doc/users_guide_snippets/**/*"] do
		if PlatformInfo.asciidoc
			if target =~ /apache/i
				type = "-a apache"
			elsif target =~ /nginx/i
				type = "-a nginx"
			else
				type = nil
			end
	  		sh "#{PlatformInfo.asciidoc} #{ASCIIDOC_FLAGS} #{type} '#{source}'"
		else
			sh "echo 'asciidoc required to build docs' > '#{target}'"
		end
	end
end

task :clobber => [:'doxygen:clobber'] do
	sh "rm -f *.html"
end

desc "Generate Doxygen C++ API documentation if necessary"
task :doxygen => ['doc/cxxapi']
file 'doc/cxxapi' => Dir['ext/apache2/*.{h,c,cpp}'] do
	sh "cd doc && doxygen"
end

desc "Force generation of Doxygen C++ API documentation"
task :'doxygen:force' do
	sh "cd doc && doxygen"
end

desc "Remove generated Doxygen C++ API documentation"
task :'doxygen:clobber' do
	sh "rm -rf doc/cxxapi"
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
	s.summary = "Easy and robust Ruby web application deployment"
	s.name = "passenger"
	s.version = VERSION_STRING
	s.rubyforge_project = "passenger"
	s.author = "Phusion - http://www.phusion.nl/"
	s.email = "info@phusion.nl"
	s.require_paths = ["lib"]
	s.add_dependency 'rake', '>= 0.8.1'
	s.add_dependency 'fastthread', '>= 1.0.1'
	s.add_dependency 'daemon_controller', '>= 0.2.5'
	s.add_dependency 'file-tail'
	s.add_dependency 'rack'
	s.files = FileList[*Packaging::GLOB]
	s.executables = Packaging::USER_EXECUTABLES + Packaging::SUPER_USER_EXECUTABLES
	s.has_rdoc = true
	s.extra_rdoc_files = ['README']
	s.rdoc_options <<
		"-S" << "-N" << "-p" << "-H" <<
		'--main' << 'README' <<
		'--title' << 'Passenger Ruby API'
	s.description = "Easy and robust Ruby web application deployment."
end

Rake::GemPackageTask.new(spec) do |pkg|
	pkg.need_tar_gz = true
end

task 'package:filelist' do
	# The package:filelist task is used by Phusion Passenger Lite
	# to obtain a list of files that it must copy to a temporary
	# directory in order to compile Nginx and to compile Phusion
	# Passenger. The original Phusion Passenger sources might not
	# be writiable, e.g. when installed via a gem as root, hence
	# the reason for copying.
	#
	# The Asciidoc HTML files are in the packaging list, but
	# they're auto-generated and aren't included in the source
	# repository, so here we ensure that they exist. This is
	# generally only for people who are developing Phusion
	# Passenger itself; users who installed Phusion Passenger
	# via a gem, tarball or native package already have the
	# HTML files.
	#
	# The reason why we don't just specify Packaging::ASCII_DOCS
	# as a task dependency is because we only want to generate
	# the HTML files if they don't already exist; we don't want
	# to regenerate if they exist but their source .txt files
	# are modified. When the user installs Phusion Passenger
	# via a gem/tarball/package, all file timestamps are set
	# to the current clock time, which could lead Rake into
	# thinking that the source .txt files are modified. Since
	# the user probably has no write permission to the original
	# Phusion Passenger sources we want to avoid trying to
	# regenerate the HTML files.
	Packaging::ASCII_DOCS.each do |ascii_doc|
		Rake::Task[ascii_doc].invoke if !File.exist?(ascii_doc)
	end
	puts spec.files
end

Rake::Task['package'].prerequisites.unshift(:doc)
Rake::Task['package:gem'].prerequisites.unshift(:doc)
Rake::Task['package:force'].prerequisites.unshift(:doc)
task :clobber => :'package:clean'

desc "Create a fakeroot, useful for building native packages"
task :fakeroot => [:apache2, :nginx] + Packaging::ASCII_DOCS do
	require 'rbconfig'
	require 'fileutils'
	include Config
	fakeroot = "pkg/fakeroot"
	
	# We don't use CONFIG['archdir'] and the like because we want
	# the files to be installed to /usr, and the Ruby interpreter
	# on the packaging machine might be in /usr/local.
	fake_libdir = "#{fakeroot}/usr/lib/ruby/#{CONFIG['ruby_version']}"
	fake_native_support_dir = "#{fakeroot}#{NATIVELY_PACKAGED_NATIVE_SUPPORT_DIR}"
	fake_agents_dir = "#{fakeroot}#{NATIVELY_PACKAGED_AGENTS_DIR}"
	fake_helper_scripts_dir = "#{fakeroot}#{NATIVELY_PACKAGED_HELPER_SCRIPTS_DIR}"
	fake_docdir = "#{fakeroot}#{NATIVELY_PACKAGED_DOCDIR}"
	fake_bindir = "#{fakeroot}/usr/bin"
	fake_sbindir = "#{fakeroot}/usr/sbin"
	fake_source_root = "#{fakeroot}#{NATIVELY_PACKAGED_SOURCE_ROOT}"
	fake_apache2_module = "#{fakeroot}#{NATIVELY_PACKAGED_APACHE2_MODULE}"
	fake_apache2_module_dir = File.dirname(fake_apache2_module)
	
	sh "rm -rf #{fakeroot}"
	sh "mkdir -p #{fakeroot}"
	
	sh "mkdir -p #{fake_libdir}"
	sh "cp #{LIBDIR}/phusion_passenger.rb #{fake_libdir}/"
	sh "cp -R #{LIBDIR}/phusion_passenger #{fake_libdir}/"
	
	sh "mkdir -p #{fake_native_support_dir}"
	Dir["#{NATIVE_SUPPORT_DIR}/*"].each do |subdir|
		next unless File.directory?(subdir)
		subdirname = File.basename(subdir)
		sh "mkdir -p #{fake_native_support_dir}/#{subdirname}"
		sh "cp -R #{subdir}/*.#{LIBEXT} #{fake_native_support_dir}/#{subdirname}/"
	end
	
	sh "mkdir -p #{fake_agents_dir}"
	sh "cp -R #{AGENTS_DIR}/* #{fake_agents_dir}/"
	sh "rm -rf #{fake_agents_dir}/*.dSYM"
	sh "rm -rf #{fake_agents_dir}/*/*.dSYM"
	
	sh "mkdir -p #{fake_helper_scripts_dir}"
	sh "cp -R #{HELPER_SCRIPTS_DIR}/* #{fake_helper_scripts_dir}/"
	
	sh "mkdir -p #{fake_docdir}"
	Packaging::ASCII_DOCS.each do |docfile|
		sh "cp", docfile, "#{fake_docdir}/"
	end
	sh "cp -R doc/images #{fake_docdir}/"
	
	sh "mkdir -p #{fake_bindir}"
	Packaging::USER_EXECUTABLES.each do |exe|
		sh "cp bin/#{exe} #{fake_bindir}/"
	end
	
	sh "mkdir -p #{fake_sbindir}"
	Packaging::SUPER_USER_EXECUTABLES.each do |exe|
		sh "cp bin/#{exe} #{fake_sbindir}/"
	end
	
	sh "mkdir -p #{fake_apache2_module_dir}"
	sh "cp #{APACHE2_MODULE} #{fake_apache2_module_dir}/"
	
	sh "mkdir -p #{fake_source_root}"
	spec.files.each do |filename|
		next if File.directory?(filename)
		dirname = File.dirname(filename)
		if !File.directory?("#{fake_source_root}/#{dirname}")
			sh "mkdir -p '#{fake_source_root}/#{dirname}'"
		end
		puts "cp '#{filename}' '#{fake_source_root}/#{dirname}/'"
		FileUtils.cp(filename, "#{fake_source_root}/#{dirname}/")
	end
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
	
	sh "sed -i 's/Version: .*/Version: #{VERSION_STRING}/' debian/control"
	sh "cp -R debian #{fakeroot}/DEBIAN"
	sh "sed -i 's/: any/: #{arch}/' #{fakeroot}/DEBIAN/control"
	sh "chown -R root:root #{fakeroot}"
	sh "dpkg -b #{fakeroot} pkg/passenger_#{VERSION_STRING}-#{arch}.deb"
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
			"test/**/*.{cpp,rb}"
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

task :compile_app => [COMMON_LIBRARY, BOOST_OXT_LIBRARY, :libev] do
	source = ENV['SOURCE'] || ENV['FILE'] || ENV['F']
	if !source
		STDERR.puts "Please specify the source filename with SOURCE=(...)"
		exit 1
	end
	exe    = source.sub(/\.cpp$/, '')
	create_executable(exe, source,
		"-Iext -Iext/common #{LIBEV_CFLAGS} " <<
		"#{PlatformInfo.portability_cflags} " <<
		"#{EXTRA_CXXFLAGS} " <<
		"#{COMMON_LIBRARY} " <<
		"#{BOOST_OXT_LIBRARY} " <<
		"#{LIBEV_LIBS} " <<
		"#{PlatformInfo.portability_ldflags} " <<
		"#{EXTRA_LDFLAGS}")
end
