shared_examples_for "MyCook(tm) beta" do
	it "is possible to fetch static assets" do
		get('/images/rails.png').should == @stub.public_file('images/rails.png')
	end
	
	it "supports page caching on file URIs" do
		get('/welcome/cached').should =~ %r{This is the cached version of /welcome/cached}
	end
	
	it "supports page caching on directory URIs" do
		get('/uploads').should =~ %r{This is the cached version of /uploads}
	end
	
	it "supports page caching on root/base URIs" do
		File.write("#{@stub.app_root}/public/index.html", "This is index.html.")
		get('/').should == "This is index.html."
	end
	
	it "doesn't use page caching if the HTTP request is not GET" do
		post('/welcome/cached').should =~ %r{This content should never be displayed}
	end
	
	# TODO: move this to module compatibility tests
	it "isn't interfered by Rails's default .htaccess dispatcher rules" do
		get('/welcome/in_passenger').should == 'true'
	end
	
	it "is possible to GET a regular Rails page" do
		get('/').should =~ /Welcome to MyCook/
	end
	
	it "is possible to pass GET parameters to a Rails page" do
		result = get('/welcome/parameters_test?hello=world&recipe[name]=Green+Bananas')
		result.should =~ %r{<hello>world</hello>}
		result.should =~ %r{<recipe>}
		result.should =~ %r{<name>Green Bananas</name>}
	end
	
	it "is possible to POST to a Rails page" do
		result = post('/recipes', {
			'recipe[name]' => 'Banana Pancakes',
			'recipe[instructions]' => 'Call 0900-BANANAPANCAKES'
		})
		result.should =~ %r{HTTP method: post}
		result.should =~ %r{Name: Banana Pancakes}
		result.should =~ %r{Instructions: Call 0900-BANANAPANCAKES}
	end
	
	it "is possible to upload a file" do
		rails_png = File.open("#{@stub.app_root}/public/images/rails.png", 'rb')
		params = {
			'upload[name1]' => 'Kotonoha',
			'upload[name2]' => 'Sekai',
			'upload[data]' => rails_png
		}
		begin
			response = post('/uploads', params)
			rails_png.rewind
			response.should ==
				"name 1 = Kotonoha\n" <<
				"name 2 = Sekai\n" <<
				"data = " << rails_png.read
		ensure
			rails_png.close
		end
	end
	
	it "supports HTTP POST with 'chunked' transfer encoding" do
		if !@web_server_supports_chunked_transfer_encoding
			# Nginx doesn't support 'chunked' transfer encoding for uploads.
			return pending
		end
		
		uri = URI.parse(@server)
		base_uri = uri.path.sub(%r(/$), '')
		socket = TCPSocket.new(uri.host, uri.port)
		begin
			socket.write("POST #{base_uri}/uploads/single HTTP/1.1\r\n")
			socket.write("Host: #{uri.host}\r\n")
			socket.write("Transfer-Encoding: chunked\r\n")
			socket.write("Content-Type: application/x-www-form-urlencoded\r\n")
			socket.write("\r\n")
			
			chunk = "foo=bar!"
			socket.write("%X\r\n%s\r\n" % [chunk.size, chunk])
			socket.write("0\r\n")
			socket.close_write
			
			lines = socket.read.split(/\r?\n/)
			lines.last.should == "bar!"
		ensure
			socket.close
		end
	end
	
	it "properly handles custom headers" do
		response = get_response('/welcome/headers_test')
		response["X-Foo"].should == "Bar"
	end
	
	it "supports restarting via restart.txt" do
		controller = "#{@stub.app_root}/app/controllers/test_controller.rb"
		restart_file = "#{@stub.app_root}/tmp/restart.txt"
		now = Time.now
		
		File.write(controller, %q{
			class TestController < ApplicationController
				layout nil
				def index
					render :text => "foo"
				end
			end
		})
		File.touch(restart_file, now - 10)
		get('/test').should == "foo"
		
		File.write(controller, %q{
			class TestController < ApplicationController
				layout nil
				def index
					render :text => "bar"
				end
			end
		})

		File.touch(restart_file, now - 5)
		get('/test').should == 'bar'
	end
	
	it "does not make the web server crash if the app crashes" do
		post('/welcome/terminate')
		sleep(0.25) # Give the app the time to terminate itself.
		get('/').should =~ /Welcome to MyCook/
	end
	
	it "does not conflict with Phusion Passenger if there's a model named 'Passenger'" do
		Dir.mkdir("#{@stub.app_root}/app/models") rescue nil
		File.write("#{@stub.app_root}/app/models/passenger.rb", %q{
			class Passenger
				def name
					return "Gourry Gabriev"
				end
			end
		})
		File.touch("#{@stub.app_root}/tmp/restart.txt")
		get('/welcome/passenger_name').should == 'Gourry Gabriev'
	end
	
	it "sets the 'Status' header" do
		response = get_response('/nonexistant')
		response["Status"].should == "404"
	end
	
	if Process.uid == 0
		it "runs as an unprivileged user" do
			post('/welcome/touch')
			begin
				stat = File.stat("#{@stub.app_root}/public/touch.txt")
				stat.uid.should_not == 0
				stat.gid.should_not == 0
			ensure
				File.unlink("#{@stub.app_root}/public/touch.txt") rescue nil
			end
		end
	end
end
