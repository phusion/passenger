app = lambda do |env|
  [200, { "Content-Type" => "text/plain" }, ["ok\n"]]
end
1 / 0
run app
