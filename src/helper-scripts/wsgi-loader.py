#!/usr/bin/env python
#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2010-2014 Phusion Holding B.V.
#
#  "Passenger", "Phusion Passenger" and "Union Station" are registered
#  trademarks of Phusion Holding B.V.
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

import sys, os, re, imp, threading, signal, traceback, socket, select, struct, logging, errno
import tempfile

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

	print("!> I have control 1.0")
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
	global options

	sys.path.insert(0, os.getcwd())
	startup_file = options.get('startup_file', 'passenger_wsgi.py')
	return imp.load_source('passenger_wsgi', startup_file)

def create_server_socket():
	global options
	
	UNIX_PATH_MAX = int(options.get('UNIX_PATH_MAX', 100))
	if 'socket_dir' in options:
		socket_dir = options['socket_dir']
		socket_prefix = 'wsgi'
	else:
		socket_dir = tempfile.gettempdir()
		socket_prefix = 'PsgWsgiApp'

	i = 0
	while i < 128:
		s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
		socket_suffix = format(struct.unpack('Q', os.urandom(8))[0], 'x')
		filename = socket_dir + '/' + socket_prefix + '.' + socket_suffix
		filename = filename[0:UNIX_PATH_MAX]
		try:
			s.bind(filename)
			break
		except socket.error as e:
			if e.errno == errno.EADDRINUSE:
				i += 1
				if i == 128:
					raise e
			else:
				raise e

	s.listen(1000)
	return (filename, s)

def install_signal_handlers():
	def debug(sig, frame):
		id2name = dict([(th.ident, th.name) for th in threading.enumerate()])
		code = []
		for thread_id, stack in sys._current_frames().items():
			code.append("\n# Thread: %s(%d)" % (id2name.get(thread_id,""), thread_id))
			for filename, lineno, name, line in traceback.extract_stack(stack):
				code.append('  File: "%s", line %d, in %s' % (filename, lineno, name))
				if line:
					code.append("    %s" % (line.strip()))
		print("\n".join(code))

	def debug_and_exit(sig, frame):
		debug(sig, frame)
		sys.exit(1)

	# Unfortunately, there's no way to install a signal handler that prints
	# the backtrace without interrupting the current system call. os.siginterrupt()
	# doesn't seem to work properly either. That is why we only have a SIGABRT
	# handler and no SIGQUIT handler.
	signal.signal(signal.SIGABRT, debug_and_exit)

def advertise_sockets(socket_filename):
	print("!> socket: main;unix:%s;session;1" % socket_filename)
	print("!> ")

if sys.version_info[0] >= 3:
	def reraise_exception(exc_info):
		raise exc_info[0].with_traceback(exc_info[1], exc_info[2])

	def bytes_to_str(b):
		return b.decode()

	def str_to_bytes(s):
		return s.encode('latin-1')
else:
	def reraise_exception(exc_info):
		exec("raise exc_info[0], exc_info[1], exc_info[2]")

	def bytes_to_str(b):
		return b

	def str_to_bytes(s):
		return s


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
				socket_hijacked = False
				try:
					try:
						env, input_stream = self.parse_request(client)
						if env:
							if env['REQUEST_METHOD'] == 'ping':
								self.process_ping(env, input_stream, client)
							else:
								socket_hijacked = self.process_request(env, input_stream, client)
					except KeyboardInterrupt:
						done = True
					except IOError:
						e = sys.exc_info()[1]
						if not getattr(e, 'passenger', False) or e.errno != errno.EPIPE:
							logging.exception("WSGI application raised an I/O exception!")
					except Exception:
						logging.exception("WSGI application raised an exception!")
				finally:
					if not socket_hijacked:
						try:
							# Shutdown the socket like this just in case the app
							# spawned a child process that keeps it open.
							client.shutdown(socket.SHUT_WR)
						except:
							pass
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
		buf = b''
		while len(buf) < 4:
			tmp = client.recv(4 - len(buf))
			if len(tmp) == 0:
				return (None, None)
			buf += tmp
		header_size = struct.unpack('>I', buf)[0]
		
		buf = b''
		while len(buf) < header_size:
			tmp = client.recv(header_size - len(buf))
			if len(tmp) == 0:
				return (None, None)
			buf += tmp
		
		headers = buf.split(b"\0")
		headers.pop() # Remove trailing "\0"
		env = {}
		i = 0
		while i < len(headers):
			env[bytes_to_str(headers[i])] = bytes_to_str(headers[i + 1])
			i += 2

		return (env, client)
	
	if hasattr(socket, '_fileobject'):
		def wrap_input_socket(self, sock):
			return socket._fileobject(sock, 'rb', 512)
	else:
		def wrap_input_socket(self, sock):
			return socket.socket.makefile(sock, 'rb', 512)

	def process_request(self, env, input_stream, output_stream):
		# The WSGI specification says that the input parameter object passed needs to
		# implement a few file-like methods. This is the reason why we "wrap" the socket._socket
		# into the _fileobject to solve this.
		#
		# Otherwise, the POST data won't be correctly retrieved by Django.
		#
		# See: http://www.python.org/dev/peps/pep-0333/#input-and-error-streams
		env['wsgi.input']        = self.wrap_input_socket(input_stream)
		env['wsgi.errors']       = sys.stderr
		env['wsgi.version']      = (1, 0)
		env['wsgi.multithread']  = False
		env['wsgi.multiprocess'] = True
		env['wsgi.run_once']	 = False
		if env.get('HTTPS','off') in ('on', '1', 'true', 'yes'):
			env['wsgi.url_scheme'] = 'https'
		else:
			env['wsgi.url_scheme'] = 'http'

		headers_set = []
		headers_sent = []
		is_head = env['REQUEST_METHOD'] == 'HEAD'

		def write(data):
			try:
				if not headers_set:
					raise AssertionError("write() before start_response()")
				elif not headers_sent:
					# Before the first output, send the stored headers.
					status, response_headers = headers_sent[:] = headers_set
					output_stream.sendall(str_to_bytes(
						'HTTP/1.1 %s\r\nStatus: %s\r\nConnection: close\r\n' %
						(status, status)))
					for header in response_headers:
						output_stream.sendall(str_to_bytes('%s: %s\r\n' % header))
					output_stream.sendall(b'\r\n')
				if not is_head:
					output_stream.sendall(data)
			except IOError:
				# Mark this exception as coming from the Phusion Passenger
				# socket and not some other socket.
				e = sys.exc_info()[1]
				setattr(e, 'passenger', True)
				raise e
		
		def start_response(status, response_headers, exc_info = None):
			if exc_info:
				try:
					if headers_sent:
						# Re-raise original exception if headers sent.
						reraise_exception(exc_info)
				finally:
					# Avoid dangling circular ref.
					exc_info = None
			elif headers_set:
				raise AssertionError("Headers already set!")
			
			headers_set[:] = [status, response_headers]
			return write
		
		# Django's django.template.base module goes through all WSGI
		# environment values, and calls each value that is a callable.
		# No idea why, but we work around that with the `do_it` parameter.
		def hijack(do_it = False):
			if do_it:
				env['passenger.hijacked_socket'] = output_stream
				return output_stream

		env['passenger.hijack'] = hijack

		result = self.app(env, start_response)
		if 'passenger.hijacked_socket' in env:
			# Socket connection hijacked. Don't do anything.
			return True
		try:
			for data in result:
				# Don't send headers until body appears.
				if data:
					write(data)
			if not headers_sent:
				# Send headers now if body was empty.
				write(b'')
		finally:
			if hasattr(result, 'close'):
				result.close()
		return False
	
	def process_ping(self, env, input_stream, output_stream):
		output_stream.sendall(b"pong")


if __name__ == "__main__":
	logging.basicConfig(
		level = logging.WARNING,
		format = "[ pid=%(process)d, time=%(asctime)s ]: %(message)s")
	if hasattr(logging, 'captureWarnings'):
		logging.captureWarnings(True)
	handshake_and_read_startup_request()
	app_module = load_app()
	socket_filename, server_socket = create_server_socket()
	install_signal_handlers()
	handler = RequestHandler(server_socket, sys.stdin, app_module.application)
	print("!> Ready")
	advertise_sockets(socket_filename)
	handler.main_loop()
	try:
		os.remove(socket_filename)
	except OSError:
		pass
