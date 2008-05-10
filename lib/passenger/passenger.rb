#  Phusion Passenger - http://www.modrails.com/
#  Copyright (C) 2008  Phusion
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

module Passenger
	ROOT = File.expand_path(File.join(File.dirname(__FILE__), "..", ".."))
	$LOAD_PATH << "#{ROOT}/ext"

	autoload 'AbstractServer',      'passenger/abstract_server'
	autoload 'HTMLTemplate',        'passenger/html_template'
	autoload 'MessageChannel',      'passenger/message_channel'
	autoload 'CGIFixed',            'passenger/cgi_fixed'
	autoload 'Application',         'passenger/application'
	autoload 'ApplicationSpawner',  'passenger/application_spawner'
	autoload 'FrameworkSpawner',    'passenger/framework_spawner'
	autoload 'SpawnManager',        'passenger/spawn_manager'
	autoload 'PlatformInfo',        'passenger/platform_info'
	autoload 'RequestHandler',      'passenger/request_handler'
	autoload 'Utils',               'passenger/utils'
	autoload 'NativeSupport',       'passenger/native_support'
	
	autoload 'VersionNotFound',     'passenger/exceptions'
	autoload 'AppInitError',        'passenger/exceptions'
	autoload 'InitializationError', 'passenger/exceptions'
	autoload 'FrameworkInitError',  'passenger/exceptions'
	autoload 'UnknownError',        'passenger/exceptions'

	@@all_loaded = false
	
	def self.load_all_classes!
		return if @@all_loaded
		AbstractServer
		HTMLTemplate
		MessageChannel
		CGIFixed
		Application
		ApplicationSpawner
		FrameworkSpawner
		SpawnManager
		PlatformInfo
		RequestHandler
		Utils
		NativeSupport
		
		VersionNotFound
		AppInitError
		InitializationError
		FrameworkInitError
		UnknownError
		@@all_loaded = true
	end
end
