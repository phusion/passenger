shared_examples_for "HelloWorld Rack application" do
	it "is possible to fetch static assets" do
		get('/rack.jpg').should == public_file('rack.jpg')
	end
	
	it "is possible to GET a regular Rack page" do
		get('/').should =~ /hello/
	end
	
	it "supports restarting via restart.txt" do
		get('/').should =~ /hello/
		File.write("#{@stub.app_root}/config.ru", %q{
			app = lambda do |env|
				[200, { "Content-Type" => "text/html" }, "changed"]
			end
			run app
		})
		File.new("#{@stub.app_root}/tmp/restart.txt", "w").close
		get('/').should == "changed"
	end
	
	if Process.uid == 0
		it "runs as an unprivileged user" do
			File.prepend("#{@stub.app_root}/config.ru", %q{
				File.new('foo.txt', 'w').close
			})
			File.new("#{@stub.app_root}/tmp/restart.txt", "w").close
			get('/')
			stat = File.stat("#{@stub.app_root}/foo.txt")
			stat.uid.should_not == 0
			stat.gid.should_not == 0
		end
	end
end
