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
	
	# We can't autoload the exception classes because their class names
	# don't match their filenames, and that'll trigger bugs in
	# ActiveSupport's Dependency autoloading code.
	require 'passenger/exceptions'
	# And for some reason, CGIFixed conflicts too.
	require 'passenger/cgi_fixed'

	autoload 'AbstractServer',         'passenger/abstract_server'
	autoload 'AbstractRequestHandler', 'passenger/abstract_request_handler'
	autoload 'HTMLTemplate',           'passenger/html_template'
	autoload 'MessageChannel',         'passenger/message_channel'
	autoload 'Application',            'passenger/application'
	autoload 'ApplicationSpawner',     'passenger/application_spawner'
	autoload 'FrameworkSpawner',       'passenger/framework_spawner'
	autoload 'SpawnManager',           'passenger/spawn_manager'
	autoload 'PlatformInfo',           'passenger/platform_info'
	autoload 'Utils',                  'passenger/utils'
	autoload 'NativeSupport',          'passenger/native_support'
	
	module Rails
		autoload 'RequestHandler', 'passenger/rails/request_handler'
	end
	
	module Rack
		autoload 'ApplicationSpawner', 'passenger/rack/application_spawner'
		autoload 'RequestHandler',     'passenger/rack/request_handler'
	end
	
	@@all_loaded = false
	
	def self.load_all_classes!
		return if @@all_loaded
		require 'cgi'
		require 'stringio'
		
		AbstractServer
		AbstractRequestHandler
		HTMLTemplate
		MessageChannel
		Application
		ApplicationSpawner
		FrameworkSpawner
		SpawnManager
		PlatformInfo
		Utils
		NativeSupport
		
		Rails::RequestHandler
		
		Rack::ApplicationSpawner
		Rack::RequestHandler
		@@all_loaded = true
	end
end
