#!/usr/bin/env python
import socket, os, random, sys, struct, select, imp
import exceptions, traceback

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
						self.process_request(env, input_stream, client)
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
			buf += tmp
		header_size = struct.unpack('>I', buf)[0]
		
		buf = ''
		while len(buf) < header_size:
			tmp = client.recv(header_size - len(buf))
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
		env['wsgi.input']        = input_stream
		env['wsgi.errors']       = sys.stderr
		env['wsgi.version']      = (1, 0)
		env['wsgi.multithread']  = False
		env['wsgi.multiprocess'] = True
		env['wsgi.run_once']     = True
		if env.get('HTTPS','off') in ('on', '1'):
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

if __name__ == "__main__":
	socket_file = sys.argv[1]
	server = socket.fromfd(int(sys.argv[2]), socket.AF_UNIX, socket.SOCK_STREAM)
	owner_pipe = int(sys.argv[3])
	
	app_module = imp.load_source('passenger_wsgi', 'passenger_wsgi.py')
	
	handler = RequestHandler(socket_file, server, owner_pipe, app_module.application)
	try:
		handler.main_loop()
	finally:
		handler.cleanup()
