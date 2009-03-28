shared_examples_for "HelloWorld WSGI application" do
	after :each do
		File.unlink("#{@stub.app_root}/passenger_wsgi.pyc") rescue nil
	end
	
	it "is possible to fetch static assets" do
		get('/wsgi-snake.jpg').should == @stub.public_file('wsgi-snake.jpg')
	end
	
	it "is possible to GET a regular WSGI page" do
		get('/').should =~ /Hello World/
	end
	
	it "supports restarting via restart.txt" do
		get('/').should =~ /Hello World/
		
		code = %q{
			def application(env, start_response):
				start_response('200 OK', [('Content-Type', 'text/html')])
				return ["changed"]
		}.gsub(/^\t\t\t/, '')
		
		File.write("#{@stub.app_root}/passenger_wsgi.py", code)
		File.new("#{@stub.app_root}/tmp/restart.txt", "w").close
		File.utime(2, 2, "#{@stub.app_root}/tmp/restart.txt")
		get('/').should == "changed"
	end
	
	if Process.uid == 0
		it "runs as an unprivileged user" do
			File.prepend("#{@stub.app_root}/passenger_wsgi.py",
				"file('foo.txt', 'w').close()\n")
			File.new("#{@stub.app_root}/tmp/restart.txt", "w").close
			File.utime(1, 1, "#{@stub.app_root}/tmp/restart.txt")
			get('/')
			stat = File.stat("#{@stub.app_root}/foo.txt")
			stat.uid.should_not == 0
			stat.gid.should_not == 0
		end
	end
end
