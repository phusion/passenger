import os, sys, time, cgi

def file_exist(filename):
	try:
		os.stat(filename)
		return True
	except OSError:
		return False

if sys.version_info[0] >= 3:
	def bytes_to_str(b):
		return b.decode()

	def str_to_bytes(s):
		if isinstance(s, bytes):
			return s
		else:
			return s.encode('latin-1')

	def iteritems(d):
		return d.items()
else:
	def bytes_to_str(b):
		return b

	def str_to_bytes(s):
		return s

	def iteritems(d):
		return d.iteritems()

def application(env, start_response):
	status = '200 OK'
	body = b""

	method = env.get('REQUEST_METHOD')
	if method == 'OOBW':
		time.sleep(1)
		start_response(status, [('Content-Type', 'text/html')])
		return [str('oobw ok')]

	filename = env.get('HTTP_X_WAIT_FOR_FILE')
	if filename is not None:
		while not file_exist(filename):
			time.sleep(0.01)

	path = env['PATH_INFO']
	if path == '/':
		if file_exist("front_page.txt"):
			with open("front_page.txt", "rb") as f:
				body = f.read()
		else:
			body = b"front page"
	elif path == '/parameters':
		method = env['REQUEST_METHOD']
		params = cgi.parse(env['wsgi.input'], env)
		first  = params['first'][0]
		second = params['second'][0]
		body = str_to_bytes("Method: %s\nFirst: %s\nSecond: %s\n" % (method, first, second))
	elif path == '/chunked':
		def bodyfn():
			yield str("7\r\nchunk1\n\r\n")
			yield str("7\r\nchunk2\n\r\n")
			yield str("7\r\nchunk3\n\r\n")
			yield str("0\r\n\r\n")
			sleep_time = float(env.get('HTTP_X_SLEEP_WHEN_DONE', 0))
			time.sleep(sleep_time)
			if env.get('HTTP_X_EXTRA_DATA') is not None:
				status = False
				try:
					yield str("7\r\nchunk4\n\r\n")
					status = True
				finally:
					filename = env.get('HTTP_X_TAIL_STATUS_FILE')
					if filename is not None:
						f = open(filename, "wb")
						try:
							f.write(str_to_bytes(str(status)))
						finally:
							f.close()
		start_response(status, [('Content-Type', 'text/html'), ('Transfer-Encoding', 'chunked')])
		return bodyfn()
	elif path == '/pid':
		body = str_to_bytes(str(os.getpid()))
	elif path.startswith('/env'):
		body = b''
		for pair in iteritems(env):
			body += str_to_bytes(pair[0] + ' = ' + str(pair[1]) + "\n")
	elif path == '/touch_file':
		params = cgi.parse(env['wsgi.input'], env)
		filename = params["file"][0]
		open(filename, 'w').close()
		body = b"ok"
	elif path == '/extra_header':
		start_response(status, [('Content-Type', 'text/html'), ('X-Foo', 'Bar')])
		return ["ok"]
	elif path == '/cached':
		body = b"This is the uncached version of /cached"
	elif path == '/upload_with_params':
		params = cgi.FieldStorage(fp = env['wsgi.input'], environ = env)
		name1 = str_to_bytes(params["name1"].value)
		name2 = str_to_bytes(params["name2"].value)
		data  = str_to_bytes(params["data"].value)
		body  = b"name 1 = " + name1 + b"\nname 2 = " + name2 + b"\ndata = " + data
	elif path == '/raw_upload_to_file':
		sleep_time = float(env.get('HTTP_X_SLEEP', 0))
		f = open(env['HTTP_X_OUTPUT'], 'wb')
		try:
			line = env['wsgi.input'].readline()
			while len(line) > 0:
				f.write(line)
				f.flush()
				line = env['wsgi.input'].readline()
				if sleep_time > 0:
					time.sleep(sleep_time)
		finally:
			f.close()
		body = b'ok'
	elif path == '/custom_status':
		status = env['HTTP_X_CUSTOM_STATUS']
		body = b'ok'
	elif path == '/stream':
		sleep_time = float(env.get('HTTP_X_SLEEP', 0.1))
		def bodyfn():
			i = 0
			while True:
				data = ' ' * 32 + str(i) + "\n"
				yield("%x\r\n" % len(data))
				yield(data)
				yield("\r\n")
				time.sleep(sleep_time)
				i += 1
		start_response(status, [('Content-Type', 'text/html'), ('Transfer-Encoding', 'chunked')])
		return bodyfn()
	elif path == '/chunked_stream':
		sleep_time = float(env.get('HTTP_X_SLEEP', 0.05))
		count = float(env.get('HTTP_X_COUNT', 3))
		def bodyfn():
			i = 0
			while i < count:
				data = "Counter: " + str(i) + "\n"
				yield("%x\r\n" % len(data))
				yield(data)
				yield("\r\n")
				time.sleep(sleep_time)
				i += 1
			yield("0\r\n\r\n")
			time.sleep(2)
		start_response(status, [('Content-Type', 'text/html'), ('Transfer-Encoding', 'chunked')])
		return bodyfn()
	elif path == '/sleep':
		sleep_time = float(env.get('HTTP_X_SLEEP', 5))
		time.sleep(sleep_time)
		status = 200
		body = b'ok'
	elif path == '/blob':
		size = int(env.get('HTTP_X_SIZE', 1024 * 1024 * 10))
		headers = [('Content-Type', 'text/plain')]
		if env.get('HTTP_X_CONTENT_LENGTH') is not None:
			headers.append(('Content-Length', size))
		def bodyfn():
			written = 0
			while written < size:
				data = 'x' * min(1024 * 8, size - written)
				yield(data)
				written += len(data)
			sleep_time = float(env.get('HTTP_X_SLEEP_WHEN_DONE', 0))
			time.sleep(sleep_time)
			if env.get('HTTP_X_EXTRA_DATA') is not None:
				status = False
				try:
					yield str("tail")
					status = True
				finally:
					filename = env.get('HTTP_X_TAIL_STATUS_FILE')
					if filename is not None:
						f = open(filename, "wb")
						try:
							f.write(str_to_bytes(str(status)))
						finally:
							f.close()
		start_response(status, headers)
		return bodyfn()
	elif path == '/oobw':
		start_response(status, [('Content-Type', 'text/plain'), ('X-Passenger-Request-OOB-Work', 'true')])
		return [str(os.getpid())]
	elif path == '/switch_protocol':
		if env['HTTP_UPGRADE'] != 'raw' or env['HTTP_CONNECTION'].lower() != 'upgrade':
			status = '500 Internal Server Error'
			body = b'Invalid headers'
			start_response(status, [('Content-Type', 'text/plain'), ('Content-Length', len(body))])
			return [body]
		socket = env['passenger.hijack'](True)
		io = socket.makefile()
		socket.close()
		try:
			io.write(
				b"HTTP/1.1 101 Switching Protocols\r\n" +
				b"Upgrade: raw\r\n" +
				b"Connection: Upgrade\r\n" +
				b"\r\n")
			io.flush()
			line = io.readline()
			while len(line) > 0:
				io.write(str_to_bytes("Echo: " + line))
				io.flush()
				line = io.readline()
		finally:
			io.close()
	else:
		status = "404 Not Found"
		body = b"Unknown URI"

	start_response(status, [('Content-Type', 'text/plain'), ('Content-Length', len(body))])
	return [body]
