# encoding: binary

def text_response(body)
	body = binary_string(body.to_s)
	return [200, { "Content-Type" => "text/plain", "Content-Length" => body.size.to_s }, [body]]
end

if "".respond_to?(:force_encoding)
	def binary_string(str)
		return str.force_encoding("binary")
	end
else
	def binary_string(str)
		return str
	end
end

app = lambda do |env|
	case env['PATH_INFO']
	when '/'
		if File.exist?("front_page.txt")
			text_response(File.read("front_page.txt"))
		else
			text_response("front page")
		end
	when '/parameters'
		req = Rack::Request.new(env)
		method = env["REQUEST_METHOD"]
		first = req.params["first"]
		second = req.params["second"]
		text_response("Method: #{method}\nFirst: #{first}\nSecond: #{second}\n")
	when '/chunked'
		chunks = ["7\r\nchunk1\n\r\n", "7\r\nchunk2\n\r\n", "7\r\nchunk3\n\r\n", "0\r\n\r\n"]
		[200, { "Content-Type" => "text/html", "Transfer-Encoding" => "chunked" }, chunks]
	when '/pid'
		text_response(Process.pid)
	when /^\/env/
		body = ''
		env.sort.each do |key, value|
			body << "#{key} = #{value}\n"
		end
		text_response(body)
	when '/touch_file'
		req = Rack::Request.new(env)
		filename = req.params["file"]
		File.open(filename, "w").close
		text_response("ok")
	when '/extra_header'
		[200, { "Content-Type" => "text/html", "X-Foo" => "Bar" }, ["ok"]]
	when '/cached'
		text_response("This is the uncached version of /cached")
	when '/upload_with_params'
		req = Rack::Request.new(env)
		name1 = binary_string(req.params["name1"])
		name2 = binary_string(req.params["name2"])
		file = req.params["data"][:tempfile]
		file.binmode
		text_response(
			"name 1 = #{name1}\n" <<
			"name 2 = #{name2}\n" <<
			"data = #{file.read}")
	when '/raw_upload_to_file'
		File.open(env['HTTP_X_OUTPUT'], 'w') do |f|
			while line = env['rack.input'].gets
				f.write(line)
				f.flush
			end
		end
		text_response("ok")
	when '/print_stderr'
		STDERR.puts "hello world!"
		text_response("ok")
	when '/print_stdout_and_stderr'
		STDOUT.puts "hello stdout!"
		sleep 0.1  # Give HelperAgent the time to process stdout first.
		STDERR.puts "hello stderr!"
		text_response("ok")
	else
		[404, { "Content-Type" => "text/plain" }, ["Unknown URI"]]
	end
end

run app
