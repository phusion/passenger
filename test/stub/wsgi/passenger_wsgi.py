import os, time

def application(env, start_response):
	path   = env['PATH_INFO']
	status = '200 OK'
	body   = None

	if path == '/pid':
		body = os.getpid()
	elif path == '/env':
		body = ''
		for pair in env.iteritems():
			body += pair[0] + ' = ' + str(pair[1]) + "\n"
		body = body
	elif path == '/upload':
		f = open(env['HTTP_X_OUTPUT'], 'w')
		try:
			line = env['wsgi.input'].readline()
			while line != "":
				f.write(line)
				f.flush()
				line = env['wsgi.input'].readline()
		finally:
			f.close()
		body = 'ok'
	elif path == '/custom_status':
		status = env['HTTP_X_CUSTOM_STATUS']
		body = 'ok'
	elif path == '/stream':
		def body():
			i = 0
			while True:
				data = ' ' * 32 + str(i) + "\n"
				yield("%x\r\n" % len(data))
				yield(data)
				yield("\r\n")
				time.sleep(0.1)
				i += 1
		start_response(status, [('Content-Type', 'text/html'), ('Transfer-Encoding', 'chunked')])
		return body()
	else:
		body = 'hello <b>world</b>'
	
	start_response(status, [('Content-Type', 'text/html')])
	return [str(body)]
