#!/usr/bin/env ruby
#  Phusion Passenger - http://www.modrails.com/
#  Copyright (c) 2010 Phusion
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

$LOAD_PATH.unshift("#{File.dirname(__FILE__)}/../lib")
$LOAD_PATH.unshift("#{File.dirname(__FILE__)}/../ext")
require 'phusion_passenger/html_template'
require 'phusion_passenger/spawn_manager'
require 'phusion_passenger/platform_info'
include PhusionPassenger

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
	
	e = AppInitError.new("Some error message", create_dummy_exception)
	render_error_page(e, 'app_exited.html', 'app_exited_during_initialization')
end

start
