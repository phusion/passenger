app = lambda do |env|
    if env['PATH_INFO'] == '/chunked'
        chunks = ["7\r\nchunk1\n\r\n", "7\r\nchunk2\n\r\n", "7\r\nchunk3\n\r\n", "0\r\n\r\n"]
        [200, { "Content-Type" => "text/html", "Transfer-Encoding" => "chunked" }, chunks]
    elsif env['PATH_INFO'] == '/pid'
        [200, { "Content-Type" => "text/html" }, [$$]]
    else
        [200, { "Content-Type" => "text/html" }, ["hello <b>world</b>"]]
    end
end
run app
