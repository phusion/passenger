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
