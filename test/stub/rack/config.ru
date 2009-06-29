app = lambda do |env|
    [200, { "Content-Type" => "text/html" }, ["hello <b>world</b>"]]
end
run app
