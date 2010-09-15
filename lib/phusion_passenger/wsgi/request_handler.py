#!/usr/bin/env python
#  Phusion Passenger - http://www.modrails.com/
#  Copyright (c) 2010 Phusion
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

import socket, os, random, sys, struct, select, imp
import exceptions, traceback

from socket import _fileobject

class RequestHandler:
	def __init__(self, socket_file, server, owner_pipe, app):
		self.socket_file = socket_file
		self.server = server
		self.owner_pipe = owner_pipe
		self.app = app
	
	def cleanup(self):
		self.server.close()
		try:
			os.remove(self.socket_file)
		except:
			pass
	
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
						traceback.print_tb(sys.exc_info()[2])
						sys.stderr.write(str(e.__class__) + ": " + e.message + "\n")
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
		env['wsgi.input']		 = _fileobject(input_stream,'r',512)
		env['wsgi.errors']		 = sys.stderr
		env['wsgi.version']		 = (1, 0)
		env['wsgi.multithread']	 = False
		env['wsgi.multiprocess'] = True
		env['wsgi.run_once']	 = True
		if env.get('HTTPS','off') in ('on', '1'):
			env['wsgi.url_scheme'] = 'https'
		else:
			env['wsgi.url_scheme'] = 'http'
			

		# The following environment variables are required by WSCI PEP #333 
		# see: http://www.python.org/dev/peps/pep-0333/#environ-variables
		if 'HTTP_CONTENT_LENGTH' in env:
			env['CONTENT_LENGTH'] = env.get('HTTP_CONTENT_LENGTH')
			
		
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

def import_error_handler(environ, start_response):
	write = start_response('500 Import Error', [('Content-type', 'text/plain')])
	write("An error occurred importing your passenger_wsgi.py")
	raise KeyboardInterrupt # oh WEIRD.

if __name__ == "__main__":
	socket_file = sys.argv[1]
	server = socket.fromfd(int(sys.argv[2]), socket.AF_UNIX, socket.SOCK_STREAM)
	owner_pipe = int(sys.argv[3])
	
	try:
		app_module = imp.load_source('passenger_wsgi', 'passenger_wsgi.py')
		handler = RequestHandler(socket_file, server, owner_pipe, app_module.application)
	except:
		handler = RequestHandler(socket_file, server, owner_pipe, import_error_handler)

	try:
		handler.main_loop()
	finally:
		handler.cleanup()
