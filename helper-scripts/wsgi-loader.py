#!/usr/bin/env python
#  Phusion Passenger - http://www.modrails.com/
#  Copyright (c) 2010, 2011, 2012 Phusion
#
#  "Phusion Passenger" is a trademark of Hongli Lai & Ninh Bui.
#
#  Permission is hereby granted, free of charge, to any person obtaining a copy
#  of this software and associated documentation files (the "Software"), to deal
#  in the Software without restriction, including without limitation the rights
#  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
#  copies of the Software, and to permit persons to whom the Software is
#  furnished to do so, subject to the following conditions:
#
#  The above copyright notice and this permission notice shall be included in
#  all copies or substantial portions of the Software.
#
#  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
#  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
#  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
#  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
#  THE SOFTWARE.

import sys, os, re, imp, traceback, socket, select, struct, logging
from socket import _fileobject

options = {}

def abort(message):
	sys.stderr.write(message + "\n")
	sys.exit(1)

def readline():
	result = sys.stdin.readline()
	if result == "":
		raise EOFError
	else:
		return result

def handshake_and_read_startup_request():
	global options

	print("I have control 1.0")
	if readline() != "You have control 1.0\n":
		abort("Invalid initialization header")
	
	line = readline()
	while line != "\n":
		result = re.split(': *', line.strip(), 2)
		name = result[0]
		value = result[1]
		options[name] = value
		line = readline()

def load_app():
	return imp.load_source('passenger_wsgi', 'passenger_wsgi.py')

def create_server_socket():
	global options

	filename = options['generation_dir'] + '/backends/wsgi.' + str(os.getpid())
	s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
	try:
		os.remove(filename)
	except OSError:
		pass
	s.bind(filename)
	s.listen(1000)
	return (filename, s)

def advertise_sockets(socket_filename):
	print("socket: main;unix:%s;session;1" % socket_filename)
	print("")


class RequestHandler:
	def __init__(self, server_socket, owner_pipe, app):
		self.server = server_socket
		self.owner_pipe = owner_pipe
		self.app = app
	
	def main_loop(self):
		done = False
		try:
			while not done:
				client, address = self.accept_connection()
				if not client:
					done = True
					break
				try:
					try:
						env, input_stream = self.parse_request(client)
						if env:
							if env['REQUEST_METHOD'] == 'ping':
								self.process_ping(env, input_stream, client)
							else:
								self.process_request(env, input_stream, client)
						else:
							done = True
					except KeyboardInterrupt:
						done = True
					except Exception, e:
						logging.exception("WSGI application raised an exception!")
				finally:
					try:
						client.close()
					except:
						pass
		except KeyboardInterrupt:
			pass

	def accept_connection(self):
		result = select.select([self.owner_pipe, self.server.fileno()], [], [])[0]
		if self.server.fileno() in result:
			return self.server.accept()
		else:
			return (None, None)
	
	def parse_request(self, client):
		buf = ''
		while len(buf) < 4:
			tmp = client.recv(4 - len(buf))
			if len(tmp) == 0:
				return (None, None)
			buf += tmp
		header_size = struct.unpack('>I', buf)[0]
		
		buf = ''
		while len(buf) < header_size:
			tmp = client.recv(header_size - len(buf))
			if len(tmp) == 0:
				return (None, None)
			buf += tmp
		
		headers = buf.split("\0")
		headers.pop() # Remove trailing "\0"
		env = {}
		i = 0
		while i < len(headers):
			env[headers[i]] = headers[i + 1]
			i += 2
		
		return (env, client)
	
	def process_request(self, env, input_stream, output_stream):
		# The WSGI speculation says that the input paramter object passed needs to
		# implement a few file-like methods. This is the reason why we "wrap" the socket._socket
		# into the _fileobject to solve this.
		#
		# Otherwise, the POST data won't be correctly retrieved by Django.
		#
		# See: http://www.python.org/dev/peps/pep-0333/#input-and-error-streams
		env['wsgi.input']		 = _fileobject(input_stream, 'r', 512)
		env['wsgi.errors']		 = sys.stderr
		env['wsgi.version']		 = (1, 0)
		env['wsgi.multithread']	 = False
		env['wsgi.multiprocess'] = True
		env['wsgi.run_once']	 = True
		if env.get('HTTPS','off') in ('on', '1', 'true', 'yes'):
			env['wsgi.url_scheme'] = 'https'
		else:
			env['wsgi.url_scheme'] = 'http'

		headers_set = []
		headers_sent = []
		
		def write(data):
			if not headers_set:
				raise AssertionError("write() before start_response()")
			elif not headers_sent:
				# Before the first output, send the stored headers.
				status, response_headers = headers_sent[:] = headers_set
				output_stream.send('Status: %s\r\n' % status)
				for header in response_headers:
					output_stream.send('%s: %s\r\n' % header)
				output_stream.send('\r\n')
			output_stream.send(data)
		
		def start_response(status, response_headers, exc_info = None):
			if exc_info:
				try:
					if headers_sent:
						# Re-raise original exception if headers sent.
						raise exc_info[0], exc_info[1], exc_info[2]
				finally:
					# Avoid dangling circular ref.
					exc_info = None
			elif headers_set:
				raise AssertionError("Headers already set!")
			
			headers_set[:] = [status, response_headers]
			return write
		
		result = self.app(env, start_response)
		try:
			for data in result:
				# Don't send headers until body appears.
				if data:
					write(data)
			if not headers_sent:
				# Send headers now if body was empty.
				write('')
		finally:
			if hasattr(result, 'close'):
				result.close()
	
	def process_ping(self, env, input_stream, output_stream):
		output_stream.send("pong")


if __name__ == "__main__":
	logging.basicConfig(
		level = logging.WARNING,
		format = "[ pid=%(process)d, time=%(asctime)s ]: %(message)s")
	if hasattr(logging, 'captureWarnings'):
		logging.captureWarnings(True)
	handshake_and_read_startup_request()
	app_module = load_app()
	socket_filename, server_socket = create_server_socket()
	handler = RequestHandler(server_socket, sys.stdin, app_module.application)
	print("Ready")
	advertise_sockets(socket_filename)
	handler.main_loop()
