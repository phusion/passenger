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
	$LOAD_PATH.unshift("#{ROOT}/ext")
	
	autoload 'AbstractServer',         'passenger/abstract_server'
	autoload 'AbstractRequestHandler', 'passenger/abstract_request_handler'
	autoload 'HTMLTemplate',           'passenger/html_template'
	autoload 'MessageChannel',         'passenger/message_channel'
	autoload 'Application',            'passenger/application'
	autoload 'SpawnManager',           'passenger/spawn_manager'
	autoload 'PlatformInfo',           'passenger/platform_info'
	autoload 'Utils',                  'passenger/utils'
	autoload 'NativeSupport',          'passenger/native_support'
	
	module Railz
		autoload 'ApplicationSpawner', 'passenger/railz/application_spawner'
		autoload 'FrameworkSpawner',   'passenger/railz/framework_spawner'
		autoload 'RequestHandler',     'passenger/railz/request_handler'
		autoload 'CGIFixed',           'passenger/railz/cgi_fixed'
	end
	
	module Rack
		autoload 'ApplicationSpawner', 'passenger/rack/application_spawner'
		autoload 'RequestHandler',     'passenger/rack/request_handler'
	end
	
	autoload 'VersionNotFound',     'passenger/exceptions'
	autoload 'AppInitError',        'passenger/exceptions'
	autoload 'InitializationError', 'passenger/exceptions'
	autoload 'FrameworkInitError',  'passenger/exceptions'
	autoload 'UnknownError',        'passenger/exceptions'
	
	@@all_loaded = false
	
	def self.load_all_classes!
		return if @@all_loaded
		require 'cgi'
		require 'stringio'
		
		require 'passenger/utils'
		require 'passenger/native_support'
		require 'passenger/abstract_server'
		require 'passenger/abstract_request_handler'
		require 'passenger/html_template'
		require 'passenger/message_channel'
		require 'passenger/application'
		require 'passenger/spawn_manager'
		require 'passenger/platform_info'
		
		require 'passenger/railz/application_spawner'
		require 'passenger/railz/framework_spawner'
		require 'passenger/railz/request_handler'
		
		require 'passenger/rack/application_spawner'
		require 'passenger/rack/request_handler'
		
		require 'passenger/exceptions'
		@@all_loaded = true
	end
end
