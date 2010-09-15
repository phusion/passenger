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

dependencies = [
	'ext/common/Watchdog.cpp',
	'ext/common/ServerInstanceDir.h',
	'ext/common/ResourceLocator.h',
	'ext/common/Utils/VariantMap.h',
	LIBBOOST_OXT,
	LIBCOMMON
]
file 'agents/PassengerWatchdog' => dependencies do
	sh "mkdir -p agents" if !File.directory?("agents")
	create_executable('agents/PassengerWatchdog',
		'ext/common/Watchdog.cpp',
		"-Iext -Iext/common #{PlatformInfo.portability_cflags} #{EXTRA_CXXFLAGS} " <<
		"#{LIBCOMMON} " <<
		"#{LIBBOOST_OXT} " <<
		"#{PlatformInfo.portability_ldflags} " <<
		"#{AGENT_LDFLAGS} " <<
		"#{EXTRA_LDFLAGS}")
end

dependencies = [
	'ext/common/LoggingAgent/Main.cpp',
	'ext/common/LoggingAgent/LoggingServer.h',
	'ext/common/LoggingAgent/RemoteSender.h',
	'ext/common/LoggingAgent/ChangeNotifier.h',
	'ext/common/LoggingAgent/DataStoreId.h',
	'ext/common/ServerInstanceDir.h',
	'ext/common/Logging.h',
	'ext/common/EventedServer.h',
	'ext/common/EventedClient.h',
	'ext/common/Utils/VariantMap.h',
	'ext/common/Utils/BlockingQueue.h',
	LIBCOMMON,
	LIBBOOST_OXT,
	:libev
]
file 'agents/PassengerLoggingAgent' => dependencies do
	sh "mkdir -p agents" if !File.directory?("agents")
	create_executable('agents/PassengerLoggingAgent',
		'ext/common/LoggingAgent/Main.cpp',
		"-Iext -Iext/common #{LIBEV_CFLAGS} " <<
		"#{PlatformInfo.curl_flags} " <<
		"#{PlatformInfo.zlib_flags} " <<
		"#{PlatformInfo.portability_cflags} #{EXTRA_CXXFLAGS} " <<
		"#{LIBCOMMON} " <<
		"#{LIBBOOST_OXT} " <<
		"#{LIBEV_LIBS} " <<
		"#{PlatformInfo.curl_libs} " <<
		"#{PlatformInfo.zlib_libs} " <<
		"#{PlatformInfo.portability_ldflags} " <<
		"#{AGENT_LDFLAGS} " <<
		"#{EXTRA_LDFLAGS}")
end

task :clean => 'common:clean' do
	sh "rm -f agents/PassengerWatchdog agents/PassengerLoggingAgent"
end