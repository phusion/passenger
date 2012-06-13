app = lambda do |env|
    case env['PATH_INFO']
    when '/hello'
        [200, { "Content-Type" => "text/html" }, "hello world"]
    when '/chunked'
        chunks = ["7\r\nchunk1\n\r\n", "7\r\nchunk2\n\r\n", "7\r\nchunk3\n\r\n", "0\r\n\r\n"]
        [200, { "Content-Type" => "text/html", "Transfer-Encoding" => "chunked" }, chunks]
    when '/pid'
        [200, { "Content-Type" => "text/plain" }, [$$]]
    when '/env'
        body = ''
        env.each_pair do |key, value|
            body << "#{key} = #{value}\n"
        end
        [200, { "Content-Type" => "text/plain" }, [body]]
    when '/upload'
        File.open(env['HTTP_X_OUTPUT'], 'w') do |f|
            while line = env['rack.input'].gets
                f.write(line)
                f.flush
            end
        end
        [200, { "Content-Type" => "text/html" }, ["ok"]]
    when '/print_stderr'
        STDERR.puts "hello world!"
        [200, { "Content-Type" => "text/html" }, ["ok"]]
    when '/print_stdout_and_stderr'
        STDOUT.puts "hello stdout!"
        sleep 0.1  # Give HelperAgent the time to process stdout first.
        STDERR.puts "hello stderr!"
        [200, { "Content-Type" => "text/html" }, ["ok"]]
    else
        [200, { "Content-Type" => "text/html" }, ["hello <b>world</b>"]]
    end
end
run app
