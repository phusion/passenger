#!/usr/bin/env ruby
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

$LOAD_PATH << "#{File.dirname(__FILE__)}/../lib"
require 'passenger/html_template'
require 'passenger/spawn_manager'
require 'passenger/platform_info'
include Passenger

if !defined?(Mysql::Error)
	module Mysql
		class Error < StandardError
		end
	end
end

def create_dummy_exception
	begin
		raise StandardError, "A dummy exception."
	rescue => e
		return e
	end
end

def create_database_exception
	begin
		raise Mysql::Error, "Cannot connect to database localhost:12345: connection refused (1234)"
	rescue => e
		return e
	end
end

def create_dummy_backtrace
	dummy_backtrace = nil
	begin
		raise StandardError, "A dummy exception."
	rescue => e
		dummy_backtrace = e.backtrace
	end
	dummy_backtrace.unshift("/webapps/foo/app/models/Foo.rb:102:in `something`")
	dummy_backtrace.unshift("/webapps/foo/config/environment.rb:10")
	return dummy_backtrace
end

def render_error_page(exception, output, template)
	exception.set_backtrace(create_dummy_backtrace)
	File.open(output, 'w') do |f|
		template = HTMLTemplate.new(template,
			:error => exception, :app_root => "/foo/bar")
		f.write(template.result)
		puts "Written '#{output}'"
	end
end

def start
	bt = create_dummy_backtrace
	
	e = FrameworkInitError.new("Some error message",
		create_dummy_exception,
		:vendor => '/webapps/foo')
	render_error_page(e, 'framework_init_error_with_vendor.html',
		'framework_init_error')
	
	e = FrameworkInitError.new("Some error message",
		create_dummy_exception,
		:version => '1.2.3')
	render_error_page(e, 'framework_init_error.html',
		'framework_init_error')
	
	e = VersionNotFound.new("Some error message", '>= 1.0.2')
	render_error_page(e, 'version_not_found.html',
		'version_not_found')
	
	e = AppInitError.new("Some error message", create_dummy_exception)
	render_error_page(e, 'app_init_error.html',
		'app_init_error')
	
	e = AppInitError.new("Some error message", create_database_exception)
	render_error_page(e, 'database_error.html',
		'database_error')
	
	e = ArgumentError.new("Some error message")
	render_error_page(e, 'invalid_app_root.html',
		'invalid_app_root')
	
	e = StandardError.new("Some error message")
	render_error_page(e, 'general_error.html',
		'general_error')
	
	e = StandardError.new("Some error message")
	render_error_page(e, 'app_exited.html', 'app_exited_during_initialization')
end

start
