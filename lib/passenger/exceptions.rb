#  Phusion Passenger - http://www.modrails.com/
#  Copyright (C) 2008  Phusion
#
#  Phusion Passenger is a trademark of Hongli Lai & Ninh Bui.
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

# Indicates that there is no Ruby on Rails version installed that satisfies
# a given Ruby on Rails Gem version specification.
class VersionNotFound < StandardError
	attr_reader :gem_version_spec
	
	# - +message+: The exception message.
	# - +gem_version_spec+: The Ruby on Rails Gem version specification that caused this error.
	def initialize(message, gem_version_spec)
		super(message)
		@gem_version_spec = gem_version_spec
	end
end

# An abstract base class for AppInitError and FrameworkInitError. This represents
# the failure when initializing something.
class InitializationError < StandardError
	# The exception that caused initialization to fail. This may be nil.
	attr_accessor :child_exception

	# Create a new InitializationError. +message+ is the error message,
	# and +child_exception+ is the exception that caused initialization
	# to fail.
	def initialize(message, child_exception = nil)
		super(message)
		@child_exception = child_exception
	end
end

# Raised when Rack::ApplicationSpawner, Railz::ApplicationSpawner,
# Railz::FrameworkSpawner or SpawnManager was unable to spawn an application,
# because the application either threw an exception or called exit.
#
# If the +child_exception+ attribute is nil, then it means that the application
# called exit.
class AppInitError < InitializationError
	attr_accessor :app_type
	
	def initialize(message, child_exception = nil, app_type = "rails")
		super(message, child_exception)
		@app_type = app_type
	end
end

# Raised when Railz::FrameworkSpawner or Railz::SpawnManager was unable to load a
# version of the Ruby on Rails framework. The +child_exception+ attribute is guaranteed
# non-nil.
class FrameworkInitError < InitializationError
	attr_reader :vendor
	attr_reader :version
	
	def initialize(message, child_exception, options)
		super(message, child_exception)
		if options[:vendor]
			@vendor = options[:vendor]
		else
			@version = options[:version]
		end
	end
end

class UnknownError < StandardError
	attr_accessor :real_class_name
	
	def initialize(message, class_name, backtrace)
		super("#{message} (#{class_name})")
		set_backtrace(backtrace)
		@real_class_name = class_name
	end
end

end # module Passenger
