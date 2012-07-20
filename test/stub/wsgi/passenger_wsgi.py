import os, time

def file_exist(filename):
	try:
		os.stat(filename)
		return True
	except OSError:
		return False

def application(env, start_response):
	path   = env['PATH_INFO']
	status = '200 OK'
	body   = None

	filename = env.get('HTTP_X_WAIT_FOR_FILE')
	if filename is not None:
		while not file_exist(filename):
			time.sleep(0.01)

	if path == '/pid':
		body = os.getpid()
	elif path == '/env':
		body = ''
		for pair in env.iteritems():
			body += pair[0] + ' = ' + str(pair[1]) + "\n"
		body = body
	elif path == '/upload':
		sleep_time = float(env.get('HTTP_X_SLEEP', 0))
		f = open(env['HTTP_X_OUTPUT'], 'w')
		try:
			line = env['wsgi.input'].readline()
			while line != "":
				f.write(line)
				f.flush()
				line = env['wsgi.input'].readline()
				if sleep_time > 0:
					time.sleep(sleep_time)
		finally:
			f.close()
		body = 'ok'
	elif path == '/custom_status':
		status = env['HTTP_X_CUSTOM_STATUS']
		body = 'ok'
	elif path == '/stream':
		sleep_time = float(env.get('HTTP_X_SLEEP', 0.1))
		def body():
			i = 0
			while True:
				data = ' ' * 32 + str(i) + "\n"
				yield("%x\r\n" % len(data))
				yield(data)
				yield("\r\n")
				time.sleep(sleep_time)
				i += 1
		start_response(status, [('Content-Type', 'text/html'), ('Transfer-Encoding', 'chunked')])
		return body()
	elif path == '/blob':
		size = int(env.get('HTTP_X_SIZE', 1024 * 1024 * 10))
		def body():
			written = 0
			while written < size:
				data = 'x' * min(1024 * 8, size - written)
				yield(data)
				written += len(data)
		start_response(status, [('Content-Type', 'text/plain')])
		return body()
	else:
		body = 'hello <b>world</b>'
	
	start_response(status, [('Content-Type', 'text/html')])
	return [str(body)]
