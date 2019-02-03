app = lambda do |env|
  [200, { "Content-Type" => "text/plain" }, ["ok\n"]]
end

run app
