$LOAD_PATH << "#{File.dirname(__FILE__)}/../lib"
require 'passenger/html_template'
require 'passenger/spawn_manager'
include Passenger

def create_dummy_exception
	begin
		raise StandardError, "A dummy exception."
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
	
	e = ArgumentError.new("Some error message")
	render_error_page(e, 'invalid_app_root.html',
		'invalid_app_root')
	
	e = StandardError.new("Some error message")
	render_error_page(e, 'general_error.html',
		'general_error')
end

start
