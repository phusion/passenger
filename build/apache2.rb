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


# apxs totally sucks. We couldn't get it working correctly
# on MacOS X (it had various problems with building universal
# binaries), so we decided to ditch it and build/install the
# Apache module ourselves.
#
# Oh, and libtool sucks too. Do we even need it anymore in 2008?

APACHE2_MODULE = APACHE2_OUTPUT_DIR + "mod_passenger.so"
APACHE2_MODULE_INPUT_FILES = {
	APACHE2_OUTPUT_DIR + 'Configuration.o' => %w(
		ext/apache2/Configuration.cpp
		ext/apache2/Configuration.h
		ext/apache2/Configuration.hpp
		ext/common/Constants.h),
	APACHE2_OUTPUT_DIR + 'Bucket.o' => %w(
		ext/apache2/Bucket.cpp
		ext/apache2/Bucket.h),
	APACHE2_OUTPUT_DIR + 'Hooks.o' => %w(
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
APACHE2_MOD_PASSENGER_O = APACHE2_OUTPUT_DIR + "mod_passenger.o"

APACHE2_MODULE_CXXFLAGS = "-Iext -Iext/common #{PlatformInfo.apache2_module_cflags} " <<
	"#{PlatformInfo.portability_cflags} #{EXTRA_CXXFLAGS}"
APACHE2_HELPER_CXXFLAGS = "-Iext -Iext/common #{PlatformInfo.portability_cflags} #{EXTRA_CXXFLAGS}"

APACHE2_MODULE_BOOST_OXT_LIBRARY = define_libboost_oxt_task("apache2",
	APACHE2_OUTPUT_DIR + "module_libboost_oxt",
	PlatformInfo.apache2_module_cflags)
APACHE2_MODULE_COMMON_LIBRARY    = define_common_library_task("apache2",
	APACHE2_OUTPUT_DIR + "module_libpassenger_common",
	PlatformInfo.apache2_module_cflags)


desc "Build Apache 2 module"
task :apache2 => [
	APACHE2_MODULE,
	AGENT_OUTPUT_DIR + 'apache2/PassengerHelperAgent',
	AGENT_OUTPUT_DIR + 'PassengerWatchdog',
	AGENT_OUTPUT_DIR + 'PassengerLoggingAgent',
	:native_support
]


# Define rules for the individual Apache 2 module source files.
APACHE2_MODULE_INPUT_FILES.each_pair do |target, sources|
	file(target => sources) do
		object_basename = File.basename(target)
		object_filename = APACHE2_OUTPUT_DIR + object_basename
		compile_cxx(sources[0], "#{APACHE2_MODULE_CXXFLAGS} -o #{object_filename}")
	end
end


dependencies = [
	APACHE2_MODULE_COMMON_LIBRARY,
	APACHE2_MODULE_BOOST_OXT_LIBRARY,
	APACHE2_MOD_PASSENGER_O,
	APACHE2_MODULE_OBJECTS
].flatten
file APACHE2_MODULE => dependencies do
	PlatformInfo.apxs2.nil?      and raise "Could not find 'apxs' or 'apxs2'."
	PlatformInfo.apache2ctl.nil? and raise "Could not find 'apachectl' or 'apache2ctl'."
	PlatformInfo.httpd.nil?      and raise "Could not find the Apache web server binary."
	
	sources = (APACHE2_MODULE_OBJECTS + [APACHE2_MOD_PASSENGER_O]).join(' ')
	linkflags =
		"#{PlatformInfo.apache2_module_cflags} " <<
		"#{PlatformInfo.portability_cflags} " <<
		"#{EXTRA_CXXFLAGS} " <<
		"#{APACHE2_MODULE_COMMON_LIBRARY} " <<
		"#{APACHE2_MODULE_BOOST_OXT_LIBRARY} " <<
		"#{PlatformInfo.apache2_module_ldflags} " <<
		"#{PlatformInfo.portability_ldflags} " <<
		"#{EXTRA_LDFLAGS} "
	
	create_shared_library(APACHE2_MODULE, sources, linkflags)
end

file APACHE2_MOD_PASSENGER_O => ['ext/apache2/mod_passenger.c'] do
	compile_c('ext/apache2/mod_passenger.c',
		"#{APACHE2_MODULE_CXXFLAGS} -o #{APACHE2_MOD_PASSENGER_O}")
end

dependencies = [
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
	LIBCOMMON,
	LIBBOOST_OXT
]
file AGENT_OUTPUT_DIR + 'apache2/PassengerHelperAgent' => dependencies do
	dir = "#{AGENT_OUTPUT_DIR}apache2"
	sh "mkdir -p #{dir}" if !File.directory?(dir)
	create_executable("#{dir}/PassengerHelperAgent",
		'ext/apache2/HelperAgent.cpp',
		"#{APACHE2_HELPER_CXXFLAGS} " <<
		"#{LIBCOMMON} " <<
		"#{LIBBOOST_OXT} " <<
		"#{PlatformInfo.portability_ldflags} " <<
		"#{AGENT_LDFLAGS} " <<
		"#{EXTRA_LDFLAGS}")
end

task :clean => 'apache2:clean'
desc "Clean all compiled Apache 2 files"
task 'apache2:clean' do
	files = APACHE2_MODULE_OBJECTS.dup
	files << APACHE2_MOD_PASSENGER_O
	files << APACHE2_MODULE
	files << AGENT_OUTPUT_DIR + "PassengerHelperAgent"
	sh("rm", "-rf", *files)
end