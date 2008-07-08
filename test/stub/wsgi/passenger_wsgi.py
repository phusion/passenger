def application(environ, start_response):
	start_response('200 OK', [('Content-type', 'text/plain'), ('X-Foo', 'bar')])
	return ['Hello World!<br><img src="wsgi-snake.png">']
