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

watchdog_libs = COMMON_LIBRARY.only(:base, 'AgentsBase.o', 'Utils/Base64.o')
dependencies = [
	'ext/common/agents/Watchdog/Main.cpp',
	'ext/common/agents/Watchdog/AgentWatcher.cpp',
	'ext/common/agents/Watchdog/HelperAgentWatcher.cpp',
	'ext/common/agents/Watchdog/LoggingAgentWatcher.cpp',
	'ext/common/agents/Watchdog/ServerInstanceDirToucher.cpp',
	'ext/common/Constants.h',
	'ext/common/ServerInstanceDir.h',
	'ext/common/ResourceLocator.h',
	'ext/common/Utils/VariantMap.h',
	LIBBOOST_OXT,
	watchdog_libs.link_objects
].flatten
file AGENT_OUTPUT_DIR + 'PassengerWatchdog' => dependencies do
	sh "mkdir -p #{AGENT_OUTPUT_DIR}" if !File.directory?(AGENT_OUTPUT_DIR)
	compile_cxx("ext/common/agents/Watchdog/Main.cpp",
		"-o #{AGENT_OUTPUT_DIR}PassengerWatchdog.o " <<
		"#{EXTRA_PRE_CXXFLAGS} " <<
		"-Iext -Iext/common " <<
		"#{AGENT_CFLAGS} #{EXTRA_CXXFLAGS}")
	create_executable(AGENT_OUTPUT_DIR + 'PassengerWatchdog',
		"#{AGENT_OUTPUT_DIR}PassengerWatchdog.o " <<
		"#{watchdog_libs.link_objects_as_string} " <<
		"#{LIBBOOST_OXT} " <<
		"#{EXTRA_PRE_CXX_LDFLAGS} " <<
		"#{PlatformInfo.portability_cxx_ldflags} " <<
		"#{AGENT_LDFLAGS} " <<
		"#{EXTRA_CXX_LDFLAGS}")
end

helper_agent_libs = COMMON_LIBRARY.
	only(:base, :other).
	exclude('AgentsStarter.o')
dependencies = [
	'ext/common/agents/HelperAgent/Main.cpp',
	'ext/common/agents/HelperAgent/RequestHandler.h',
	'ext/common/agents/HelperAgent/RequestHandler.cpp',
	'ext/common/agents/HelperAgent/ScgiRequestParser.h',
	'ext/common/agents/HelperAgent/SystemMetricsTool.cpp',
	'ext/common/Constants.h',
	'ext/common/StaticString.h',
	'ext/common/Account.h',
	'ext/common/AccountsDatabase.h',
	'ext/common/MessageServer.h',
	'ext/common/FileDescriptor.h',
	'ext/common/Logging.h',
	'ext/common/ResourceLocator.h',
	'ext/common/Utils/ProcessMetricsCollector.h',
	'ext/common/Utils/SystemMetricsCollector.h',
	'ext/common/Utils/VariantMap.h',
	'ext/common/ApplicationPool2/Pool.h',
	'ext/common/ApplicationPool2/Common.h',
	'ext/common/ApplicationPool2/SuperGroup.h',
	'ext/common/ApplicationPool2/Group.h',
	'ext/common/ApplicationPool2/Process.h',
	'ext/common/ApplicationPool2/Session.h',
	'ext/common/ApplicationPool2/Options.h',
	'ext/common/ApplicationPool2/PipeWatcher.h',
	'ext/common/ApplicationPool2/Spawner.h',
	'ext/common/ApplicationPool2/SpawnerFactory.h',
	'ext/common/ApplicationPool2/SmartSpawner.h',
	'ext/common/ApplicationPool2/DirectSpawner.h',
	'ext/common/ApplicationPool2/ErrorRenderer.h',
	LIBBOOST_OXT,
	helper_agent_libs.link_objects,
	LIBEV_TARGET,
	LIBEIO_TARGET
].flatten.compact
file AGENT_OUTPUT_DIR + 'PassengerHelperAgent' => dependencies do
	sh "mkdir -p #{AGENT_OUTPUT_DIR}" if !File.directory?(AGENT_OUTPUT_DIR)
	compile_cxx("ext/common/agents/HelperAgent/Main.cpp",
		"-o #{AGENT_OUTPUT_DIR}PassengerHelperAgent.o " <<
		"#{EXTRA_PRE_CXXFLAGS} " <<
		"-Iext -Iext/common " <<
		"#{AGENT_CFLAGS} #{LIBEV_CFLAGS} #{LIBEIO_CFLAGS} " <<
		"#{EXTRA_CXXFLAGS}")
	create_executable("#{AGENT_OUTPUT_DIR}PassengerHelperAgent",
		"#{AGENT_OUTPUT_DIR}PassengerHelperAgent.o",
		"#{helper_agent_libs.link_objects_as_string} " <<
		"#{LIBBOOST_OXT} " <<
		"#{EXTRA_PRE_CXX_LDFLAGS} " <<
		"#{LIBEV_LIBS} " <<
		"#{LIBEIO_LIBS} " <<
		"#{PlatformInfo.portability_cxx_ldflags} " <<
		"#{AGENT_LDFLAGS} " <<
		"#{EXTRA_CXX_LDFLAGS}")
end

logging_agent_libs = COMMON_LIBRARY.only(:base, :logging_agent, 'AgentsBase.o',
	'Utils/Base64.o', 'Utils/MD5.o', 'Utils/jsoncpp.o')
dependencies = [
	'ext/common/agents/LoggingAgent/Main.cpp',
	'ext/common/agents/LoggingAgent/AdminController.h',
	'ext/common/agents/LoggingAgent/LoggingServer.h',
	'ext/common/agents/LoggingAgent/RemoteSender.h',
	'ext/common/agents/LoggingAgent/DataStoreId.h',
	'ext/common/agents/LoggingAgent/FilterSupport.h',
	'ext/common/Constants.h',
	'ext/common/ServerInstanceDir.h',
	'ext/common/Logging.h',
	'ext/common/EventedServer.h',
	'ext/common/EventedClient.h',
	'ext/common/Utils/VariantMap.h',
	'ext/common/Utils/BlockingQueue.h',
	logging_agent_libs.link_objects,
	LIBBOOST_OXT,
	LIBEV_TARGET
].flatten.compact
file AGENT_OUTPUT_DIR + 'PassengerLoggingAgent' => dependencies do
	sh "mkdir -p #{AGENT_OUTPUT_DIR}" if !File.directory?(AGENT_OUTPUT_DIR)
	compile_cxx("ext/common/agents/LoggingAgent/Main.cpp",
		"-o #{AGENT_OUTPUT_DIR}PassengerLoggingAgent.o " <<
		"#{EXTRA_PRE_CXXFLAGS} " <<
		"-Iext -Iext/common " <<
		"#{AGENT_CFLAGS} #{LIBEV_CFLAGS} " <<
		"#{PlatformInfo.curl_flags} " <<
		"#{PlatformInfo.zlib_flags} " <<
		"#{EXTRA_CXXFLAGS}")
	create_executable("#{AGENT_OUTPUT_DIR}PassengerLoggingAgent",
		"#{AGENT_OUTPUT_DIR}PassengerLoggingAgent.o",
		"#{logging_agent_libs.link_objects_as_string} " <<
		"#{LIBBOOST_OXT} " <<
		"#{EXTRA_PRE_CXX_LDFLAGS} " <<
		"#{LIBEV_LIBS} " <<
		"#{PlatformInfo.curl_libs} " <<
		"#{PlatformInfo.zlib_libs} " <<
		"#{PlatformInfo.portability_cxx_ldflags} " <<
		"#{AGENT_LDFLAGS} " <<
		"#{EXTRA_CXX_LDFLAGS}")
end

spawn_preparer_libs = COMMON_LIBRARY.only('Utils/Base64.o', 'Utils/SystemTime.o',
	'Utils/StrIntUtils.o', 'Utils/IOUtils.o')
dependencies = [
	'ext/common/agents/SpawnPreparer.cpp',
	spawn_preparer_libs.link_objects,
	LIBBOOST_OXT
].flatten
file AGENT_OUTPUT_DIR + 'SpawnPreparer' => dependencies do
	sh "mkdir -p #{AGENT_OUTPUT_DIR}" if !File.directory?(AGENT_OUTPUT_DIR)
	create_executable(AGENT_OUTPUT_DIR + 'SpawnPreparer',
		'ext/common/agents/SpawnPreparer.cpp',
		"#{EXTRA_PRE_CXXFLAGS} #{EXTRA_PRE_CXX_LDFLAGS} " <<
		"-Iext -Iext/common " <<
		"#{AGENT_CFLAGS} #{EXTRA_CXXFLAGS} " <<
		"#{spawn_preparer_libs.link_objects_as_string} " <<
		"#{LIBBOOST_OXT} " <<
		"#{PlatformInfo.portability_cxx_ldflags} " <<
		"#{EXTRA_CXX_LDFLAGS}")
end

file AGENT_OUTPUT_DIR + 'EnvPrinter' => 'ext/common/agents/EnvPrinter.c' do
	sh "mkdir -p #{AGENT_OUTPUT_DIR}" if !File.directory?(AGENT_OUTPUT_DIR)
	create_c_executable(AGENT_OUTPUT_DIR + 'EnvPrinter',
		'ext/common/agents/EnvPrinter.c')
end

file AGENT_OUTPUT_DIR + 'TempDirToucher' => 'ext/common/agents/TempDirToucher.c' do
	sh "mkdir -p #{AGENT_OUTPUT_DIR}" if !File.directory?(AGENT_OUTPUT_DIR)
	create_c_executable(AGENT_OUTPUT_DIR + 'TempDirToucher',
		'ext/common/agents/TempDirToucher.c')
end

task 'common:clean' do
	sh "rm -rf #{AGENT_OUTPUT_DIR}"
end
