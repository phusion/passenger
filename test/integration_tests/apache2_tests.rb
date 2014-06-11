require File.expand_path(File.dirname(__FILE__) + "/spec_helper")
require 'socket'
require 'fileutils'
require 'support/apache2_controller'
PhusionPassenger.require_passenger_lib 'platform_info'
PhusionPassenger.require_passenger_lib 'admin_tools'
PhusionPassenger.require_passenger_lib 'admin_tools/server_instance'

require 'integration_tests/shared/example_webapp_tests'

# TODO: test the 'PassengerUserSwitching' and 'PassengerDefaultUser' option.
# TODO: test custom page caching directory

describe "Apache 2 module" do
	before :all do
		check_hosts_configuration
		@passenger_temp_dir = "/tmp/passenger-test.#{$$}"
		Dir.mkdir(@passenger_temp_dir)
		FileUtils.chmod_R(0777, @passenger_temp_dir)
		ENV['PASSENGER_TEMP_DIR'] = @passenger_temp_dir
	end

	after :all do
		@apache2.stop if @apache2
		FileUtils.chmod_R(0777, @passenger_temp_dir)
		FileUtils.rm_rf(@passenger_temp_dir)
	end

	before :each do
		File.open("test.log", "a") do |f|
			# Make sure that all Apache log output is prepended by the test description
			# so that we know which messages are associated with which tests.
			f.puts "\n#### #{Time.now}: #{example.full_description}"
		end
	end

	def create_apache2_controller
		@apache2 = Apache2Controller.new
		@apache2.set(:passenger_temp_dir => @passenger_temp_dir)
		if Process.uid == 0
			@apache2.set(
				:www_user => CONFIG['normal_user_1'],
				:www_group => Etc.getgrgid(Etc.getpwnam(CONFIG['normal_user_1']).gid).name
			)
		end
	end

	describe "a Ruby app running on the root URI" do
		before :all do
			create_apache2_controller
			@server = "http://1.passenger.test:#{@apache2.port}"
			@stub = RackStub.new('rack')
			@apache2 << "RailsMaxPoolSize 1"
			@apache2.set_vhost("1.passenger.test", "#{@stub.full_app_root}/public")
			@apache2.start
		end

		after :all do
			@stub.destroy
			@apache2.stop if @apache2
		end

		before :each do
			@stub.reset
		end

		it_should_behave_like "an example web app"
	end

	describe "a Ruby app running in a sub-URI" do
		before :all do
			create_apache2_controller
			@server = "http://1.passenger.test:#{@apache2.port}/subapp"
			@stub = RackStub.new('rack')
			@apache2 << "RailsMaxPoolSize 1"
			@apache2.set_vhost("1.passenger.test", File.expand_path("stub")) do |vhost|
				vhost << %Q{
					Alias /subapp #{@stub.full_app_root}/public
					<Location /subapp>
						PassengerBaseURI /subapp
						PassengerAppRoot #{@stub.full_app_root}
					</Location>
				}
			end
			@apache2.start
		end

		after :all do
			@stub.destroy
			@apache2.stop if @apache2
		end

		before :each do
			@stub.reset
		end

		it_should_behave_like "an example web app"

		it "does not interfere with the root website" do
			@server = "http://1.passenger.test:#{@apache2.port}"
			get('/').should == "This is the stub directory."
		end
	end

	describe "a Python app running on the root URI" do
		before :all do
			create_apache2_controller
			@server = "http://1.passenger.test:#{@apache2.port}"
			@stub = PythonStub.new('wsgi')
			@apache2 << "RailsMaxPoolSize 1"
			@apache2.set_vhost("1.passenger.test", "#{@stub.full_app_root}/public")
			@apache2.start
		end

		after :all do
			@stub.destroy
			@apache2.stop if @apache2
		end

		before :each do
			@stub.reset
		end

		it_should_behave_like "an example web app"
	end

	describe "a Python app running in a sub-URI" do
		before :all do
			create_apache2_controller
			@server = "http://1.passenger.test:#{@apache2.port}/subapp"
			@stub = PythonStub.new('wsgi')
			@apache2 << "RailsMaxPoolSize 1"
			@apache2.set_vhost("1.passenger.test", File.expand_path("stub")) do |vhost|
				vhost << %Q{
					Alias /subapp #{@stub.full_app_root}/public
					<Location /subapp>
						PassengerBaseURI /subapp
						PassengerAppRoot #{@stub.full_app_root}
					</Location>
				}
			end
			@apache2.start
		end

		after :all do
			@stub.destroy
			@apache2.stop if @apache2
		end

		before :each do
			@stub.reset
		end

		it_should_behave_like "an example web app"

		it "does not interfere with the root website" do
			@server = "http://1.passenger.test:#{@apache2.port}"
			get('/').should == "This is the stub directory."
		end
	end

	describe "a Node.js app running on the root URI" do
		before :all do
			create_apache2_controller
			@server = "http://1.passenger.test:#{@apache2.port}"
			@stub = NodejsStub.new('node')
			@apache2 << "RailsMaxPoolSize 1"
			@apache2.set_vhost("1.passenger.test", "#{@stub.full_app_root}/public")
			@apache2.start
		end

		after :all do
			@stub.destroy
			@apache2.stop if @apache2
		end

		before :each do
			@stub.reset
		end

		it_should_behave_like "an example web app"
	end

	describe "a Node.js app running in a sub-URI" do
		before :all do
			create_apache2_controller
			@server = "http://1.passenger.test:#{@apache2.port}/subapp"
			@stub = NodejsStub.new('node')
			@apache2 << "RailsMaxPoolSize 1"
			@apache2.set_vhost("1.passenger.test", File.expand_path("stub")) do |vhost|
				vhost << %Q{
					Alias /subapp #{@stub.full_app_root}/public
					<Location /subapp>
						PassengerBaseURI /subapp
						PassengerAppRoot #{@stub.full_app_root}
					</Location>
				}
			end
			@apache2.start
		end

		after :all do
			@stub.destroy
			@apache2.stop if @apache2
		end

		before :each do
			@stub.reset
		end

		it_should_behave_like "an example web app"

		it "does not interfere with the root website" do
			@server = "http://1.passenger.test:#{@apache2.port}"
			get('/').should == "This is the stub directory."
		end
	end

	describe "compatibility with other modules" do
		before :all do
			create_apache2_controller
			@apache2 << "RailsMaxPoolSize 1"

			@stub = RackStub.new('rack')
			@server = "http://1.passenger.test:#{@apache2.port}"
			@apache2 << "RailsMaxPoolSize 1"
			@apache2.set_vhost("1.passenger.test", "#{@stub.full_app_root}/public") do |vhost|
				vhost << "RewriteEngine on"
				vhost << "RewriteRule ^/rewritten_frontpage$ / [PT,QSA,L]"
				vhost << "RewriteRule ^/rewritten_env$ /env [PT,QSA,L]"
			end
			@apache2.start
		end

		after :all do
			@stub.destroy
			@apache2.stop if @apache2
		end

		before :each do
			@stub.reset
		end

		it "supports environment variable passing through mod_env" do
			File.write("#{@stub.app_root}/public/.htaccess", 'SetEnv FOO "Foo Bar!"')
			File.touch("#{@stub.app_root}/tmp/restart.txt", 2)  # Activate ENV changes.
			get('/env').should =~ /^FOO = Foo Bar\!$/
		end

		it "supports mod_rewrite in the virtual host block" do
			get('/rewritten_frontpage').should == "front page"
			cgi_envs = get('/rewritten_env?foo=bar+baz')
			cgi_envs.should include("REQUEST_URI = /env?foo=bar+baz\n")
			cgi_envs.should include("PATH_INFO = /env\n")
		end

		it "supports mod_rewrite in .htaccess" do
			File.write("#{@stub.app_root}/public/.htaccess", %Q{
				RewriteEngine on
				RewriteRule ^htaccess_frontpage$ / [PT,QSA,L]
				RewriteRule ^htaccess_env$ env [PT,QSA,L]
			})
			get('/htaccess_frontpage').should == "front page"
			cgi_envs = get('/htaccess_env?foo=bar+baz')
			cgi_envs.should include("REQUEST_URI = /env?foo=bar+baz\n")
			cgi_envs.should include("PATH_INFO = /env\n")
		end
	end

	describe "configuration options" do
		before :all do
			create_apache2_controller
			@apache2 << "PassengerMaxPoolSize 3"

			@stub = RackStub.new('rack')
			@stub_url_root = "http://5.passenger.test:#{@apache2.port}"
			@apache2.set_vhost('5.passenger.test', "#{@stub.full_app_root}/public") do |vhost|
				vhost << "PassengerBufferUpload off"
				vhost << "PassengerFriendlyErrorPages on"
				vhost << "AllowEncodedSlashes on"
			end

			@stub2 = RackStub.new('rack')
			@stub2_url_root = "http://6.passenger.test:#{@apache2.port}"
			@apache2.set_vhost('6.passenger.test', "#{@stub2.full_app_root}/public") do |vhost|
				vhost << "RailsEnv development"
				vhost << "PassengerSpawnMethod conservative"
				vhost << "PassengerRestartDir #{@stub2.full_app_root}/public"
				vhost << "AllowEncodedSlashes off"
			end

			@apache2.start
		end

		after :all do
			@stub.destroy
			@stub2.destroy
			@apache2.stop if @apache2
		end

		before :each do
			@stub.reset
			@stub2.reset
		end

		specify "RailsEnv is per-virtual host" do
			@server = @stub_url_root
			get('/system_env').should =~ /RAILS_ENV = production/

			@server = @stub2_url_root
			get('/system_env').should =~ /RAILS_ENV = development/
		end

		it "looks for restart.txt in the directory specified by PassengerRestartDir" do
			@server = @stub2_url_root
			startup_file = "#{@stub2.app_root}/config.ru"
			restart_file = "#{@stub2.app_root}/public/restart.txt"

			File.write(startup_file, %Q{
				require File.expand_path(File.dirname(__FILE__) + "/library")

				app = lambda do |env|
					case env['PATH_INFO']
					when '/'
						text_response("hello world")
					else
						[404, { "Content-Type" => "text/plain" }, ["Unknown URI"]]
					end
				end

				run app
			})

			now = Time.now
			File.touch(restart_file, now - 5)
			get('/').should == "hello world"

			File.write(startup_file, %Q{
				require File.expand_path(File.dirname(__FILE__) + "/library")

				app = lambda do |env|
					case env['PATH_INFO']
					when '/'
						text_response("oh hai")
					else
						[404, { "Content-Type" => "text/plain" }, ["Unknown URI"]]
					end
				end

				run app
			})

			File.touch(restart_file, now - 10)
			get('/').should == "oh hai"
		end

		describe "PassengerAppRoot" do
			before :each do
				@server = @stub_url_root
				File.write("#{@stub.full_app_root}/public/cached.html", "Static cached.html")
				File.write("#{@stub.full_app_root}/public/dir.html", "Static dir.html")
				Dir.mkdir("#{@stub.full_app_root}/public/dir")
			end

			it "supports page caching on non-index URIs" do
				get('/cached').should == "Static cached.html"
			end

			it "supports page caching on directory index URIs" do
				get('/dir').should == "Static dir.html"
			end

			it "works" do
				result = get('/parameters?first=one&second=Green+Bananas')
				result.should =~ %r{First: one\n}
				result.should =~ %r{Second: Green Bananas\n}
			end
		end

		it "resolves symlinks in the document root if PassengerResolveSymlinksInDocumentRoot is set" do
			orig_app_root = @stub.app_root
			@stub.move(File.expand_path('tmp.mycook.symlinktest'))
			FileUtils.mkdir_p(orig_app_root)
			File.symlink("#{@stub.app_root}/public", "#{orig_app_root}/public")
			begin
				File.write("#{@stub.app_root}/public/.htaccess", "PassengerResolveSymlinksInDocumentRoot on")
				@server = @stub_url_root
				get('/').should == "front page"
			ensure
				FileUtils.rm_rf(orig_app_root)
				@stub.move(orig_app_root)
			end
		end

		it "supports encoded slashes in the URL if AllowEncodedSlashes is turned on" do
			@server = @stub_url_root
			get('/env/foo%2fbar').should =~ %r{PATH_INFO = /env/foo/bar\n}

			@server = @stub2_url_root
			get('/env/foo%2fbar').should =~ %r{404 Not Found}
		end

		describe "when handling POST requests with 'chunked' transfer encoding, if PassengerBufferUpload is off" do
			it "sets Transfer-Encoding to 'chunked' and removes Content-Length" do
				@uri = URI.parse(@stub_url_root)
				socket = TCPSocket.new(@uri.host, @uri.port)
				begin
					socket.write("POST #{@stub_url_root}/env HTTP/1.1\r\n")
					socket.write("Host: #{@uri.host}:#{@uri.port}\r\n")
					socket.write("Transfer-Encoding: chunked\r\n")
					socket.write("Content-Type: text/plain\r\n")
					socket.write("Connection: close\r\n")
					socket.write("\r\n")

					chunk = "foo=bar!"
					socket.write("%X\r\n%s\r\n" % [chunk.size, chunk])
					socket.write("0\r\n\r\n")
					socket.flush

					response = socket.read
					response.should_not include("CONTENT_LENGTH = ")
					response.should include("HTTP_TRANSFER_ENCODING = chunked\n")
				ensure
					socket.close
				end
			end
		end

		####################################
	end

	describe "error handling" do
		before :all do
			create_apache2_controller
			FileUtils.rm_rf('tmp.webdir')
			FileUtils.mkdir_p('tmp.webdir')
			@webdir = File.expand_path('tmp.webdir')
			@apache2.set_vhost('1.passenger.test', @webdir) do |vhost|
				vhost << "RailsBaseURI /app-that-crashes-during-startup/public"
			end

			@stub = RackStub.new('rack')
			@stub_url_root = "http://2.passenger.test:#{@apache2.port}"
			@apache2.set_vhost('2.passenger.test', "#{@stub.full_app_root}/public")

			@apache2.start
		end

		after :all do
			FileUtils.rm_rf('tmp.webdir')
			@stub.destroy
			@apache2.stop if @apache2
		end

		before :each do
			@server = "http://1.passenger.test:#{@apache2.port}"
			@error_page_signature = /<meta name="generator" content="Phusion Passenger">/
			@stub.reset
		end

		it "displays an error page if the application crashes during startup" do
			RackStub.use('rack', "#{@webdir}/app-that-crashes-during-startup") do |stub|
				File.prepend(stub.startup_file, "raise 'app crash'")
				result = get("/app-that-crashes-during-startup/public")
				result.should =~ @error_page_signature
				result.should =~ /app crash/
			end
		end

		it "doesn't display a Ruby spawn error page if PassengerFriendlyErrorPages is off" do
			RackStub.use('rack', "#{@webdir}/app-that-crashes-during-startup") do |stub|
				File.write("#{stub.app_root}/public/.htaccess", "PassengerFriendlyErrorPages off")
				File.prepend(stub.startup_file, "raise 'app crash'")
				result = get("/app-that-crashes-during-startup/public")
				result.should_not =~ @error_page_signature
				result.should_not =~ /app crash/
			end
		end
	end

	describe "HelperAgent" do
		AdminTools = PhusionPassenger::AdminTools

		before :all do
			create_apache2_controller
			@stub = RackStub.new('rack')
			@stub_url_root = "http://1.passenger.test:#{@apache2.port}"
			@apache2.set_vhost('1.passenger.test', "#{@stub.full_app_root}/public")
			@apache2.start
			@server = "http://1.passenger.test:#{@apache2.port}"
		end

		after :all do
			@stub.destroy
			@apache2.stop if @apache2
		end

		before :each do
			@stub.reset
		end

		it "is restarted if it crashes" do
			# Make sure that all Apache worker processes have connected to
			# the helper server.
			10.times do
				get('/').should == "front page"
				sleep 0.1
			end

			# Now kill the helper server.
			instance = AdminTools::ServerInstance.list.first
			Process.kill('SIGKILL', instance.helper_agent_pid)
			sleep 0.02 # Give the signal a small amount of time to take effect.

			# Each worker process should detect that the old
			# helper server has died, and should reconnect.
			10.times do
				get('/').should == "front page"
				sleep 0.1
			end
		end

		it "exposes the application pool for passenger-status" do
			File.touch("#{@stub.app_root}/tmp/restart.txt", 1)  # Get rid of all previous app processes.
			get('/').should == "front page"
			instance = AdminTools::ServerInstance.list.first

			# Wait until the server has processed the session close event.
			sleep 0.1

			processes = instance.connect(:role => :passenger_status) do |client|
				instance.processes(client)
			end
			processes.should have(1).item
			processes[0].group.name.should == @stub.full_app_root + "#default"
			processes[0].processed.should == 1
		end
	end

	##### Helper methods #####

	def start_web_server_if_necessary
		if !@apache2.running?
			@apache2.start
		end
	end
end
