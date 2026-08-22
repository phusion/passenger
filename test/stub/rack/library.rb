# encoding: binary

def text_response(body)
  body = body.to_s.b
  [ 200, { 'Content-Type' => 'text/plain', 'Content-Length' => body.size.to_s }, [ body ] ]
end
