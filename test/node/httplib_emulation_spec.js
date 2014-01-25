var Helper = require('./spec_helper').Helper;
var FakeStream = require('./spec_helper').FakeStream;
var should = require('should');
var assert = require('assert');
var net = require('net');
var HttplibEmulation = require('phusion_passenger/httplib_emulation');

/*
 * Caveat:
 * According to the Node.js source code, when a stream is set to flowing mode,
 * it is supposed to set _readableState.flowing to true. Yet from empirical tests
 * with http.Server, this does not happen. Neither on the IncomingMessage object,
 * nor on the socket object. Therefore, in the flowing mode tests, we only check
 * the flowing mode flag on the request object, not the socket.
 */
describe('HttplibEmulation', function() {
	this.timeout(1000);

	beforeEach(function() {
		var state = this.state = {};

		state.createSocket = function(callback) {
			if (state.server || state.client) {
				throw new Error('createSocket() may only be called once');
			}

			var server = net.createServer();

			function maybeDone() {
				if (state.serverSocket && state.client) {
					callback(state.serverSocket, state.client);
				}
			}

			server.listen(0, '127.0.0.1', function() {
				var client = new net.Socket();
				client.once('connect', function() {
					state.client = client;
					maybeDone();
				});
				client.connect(server.address().port, '127.0.0.1');
			});
			server.once('connection', function(socket) {
				state.server = server;
				state.serverSocket = socket;
				maybeDone();
			});
		}
	});

	afterEach(function(done) {
		var state = this.state;
		var events = 1;
		var counter = 0;

		function maybeDone() {
			counter++;
			if (counter == events) {
				done();
			}
		}

		if (state.server) {
			events += 1;
			state.server.close(maybeDone);
			state.serverSocket.destroy();
		}
		if (state.client) {
			state.client.destroy();
		}
		maybeDone();
	});

	function createHeaders(object) {
		var key, keys = [];
		var result = {
			'SERVER_PROTOCOL': 'HTTP/1.1',
			'REMOTE_ADDR': '127.0.0.1',
			'REMOTE_PORT': '3000',
			'REQUEST_METHOD': 'GET',
			'REQUEST_URI': '/',
			'PATH_INFO': '/'
		};
		for (key in object) {
			result[key] = object[key];
		}
		for (key in result) {
			keys.push(key);
		}
		result.keys = keys;
		return result;
	}

	describe('the request object', function() {
		beforeEach(function() {
			var state = this.state;
			state.setup = function(headers, callback) {
				if (!callback) {
					callback = headers;
					headers = {};
				}
				state.headers = createHeaders(headers);
				state.createSocket(function(serverSocket, client) {
					state.req = HttplibEmulation.createIncomingMessage(
						state.headers, serverSocket, "");
					callback();
				});
			}
		});

		specify('.on() returns the request object', function(done) {
			var state = this.state;
			state.setup(function() {
				var result = state.req.on('foo', function() {});
				assert.strictEqual(result, state.req);
				done();
			});
		});

		it('sets no "upgrade" flag if there is no Upgrade header', function(done) {
			var state = this.state;
			state.setup(function() {
				assert.ok(!state.req.upgrade);
				done();
			});
		});

		it('sets the "upgrade" flag if there is an Upgrade header', function(done) {
			var state = this.state;
			var headers = {
				'HTTP_UPGRADE': 'WebSocket'
			};
			state.setup(headers, function() {
				assert.ok(state.req.upgrade);
				done();
			});
		});
	});

	describe('if the request may have a request body', function() {
		beforeEach(function() {
			var state = this.state;
			state.setup = function(headers, callback) {
				if (!callback) {
					callback = headers;
					headers = {
						'REQUEST_METHOD': 'POST'
					};
				}
				state.headers = createHeaders(headers);
				state.createSocket(function(serverSocket, client) {
					state.req = HttplibEmulation.createIncomingMessage(
						state.headers, serverSocket, "");
					callback();
				});
			}
		});

		it("isn't in flowing mode by default", function(done) {
			var state = this.state;
			state.setup(function() {
				assert.strictEqual(state.req._flowing, undefined);
				done();
			});
		});

		it("is set to flowing mode upon calling pause", function(done) {
			var state = this.state;
			state.setup(function() {
				state.req.pause();
				assert.strictEqual(state.req._flowing, false);
				done();
			});
		});

		it("is set to flowing mode upon calling resume", function(done) {
			var state = this.state;
			state.setup(function() {
				state.req.resume();
				assert.strictEqual(state.req._flowing, true);
				done();
			});
		});

		it("is set to flowing mode upon attaching a data event handler", function(done) {
			var state = this.state;
			state.setup(function() {
				var chunks = [];
				state.req.on('data', function(chunk) {
					chunks.push(chunk.toString('utf-8'));
				});
				assert.strictEqual(state.req._flowing, true);
				state.client.write("hello");
				Helper.eventually(100, function() {
					return chunks.length > 0;
				}, function() {
					chunks.should.eql(['hello']);
					done();
				});
			});
		});

		describe("when in flowing mode", function() {
			beforeEach(function(done) {
				var state = this.state;
				state.setup(function() {
					state.req.resume();
					assert.ok(state.req._flowing);
					done();
				});
			});

			specify("the request object emits data events as data is received", function(done) {
				var state = this.state;
				var chunks = [];

				state.req.on('data', function(chunk) {
					chunks.push(chunk.toString('utf-8'));
				});
				state.client.write("hello");

				Helper.eventually(100, function() {
					return chunks.length > 0;
				}, function() {
					chunks.should.eql(["hello"]);
					done();
				});
			});

			specify("the request object emits the end event after the client closes the socket", function(done) {
				var state = this.state;
				var finished;

				function endReachedPrematurely() {
					assert.fail("end event received prematurely");
					finished = true;
					done();
				}
				state.req.once('end', endReachedPrematurely);

				setTimeout(function() {
					if (!finished) {
						state.req.removeListener('end', endReachedPrematurely);
						state.req.once('end', function() {
							done();
						});
						state.client.destroy();
					}
				}, 50);
			});
		});

		describe("when in non-flowing mode", function() {
			beforeEach(function(done) {
				var state = this.state;
				state.setup(function() {
					assert.ok(!state.req._flowing);
					done();
				});
			});

			specify("the request object emits readable events upon receiving data", function(done) {
				var state = this.state;
				var readable = 0;
				state.req.on('readable', function() {
					readable++;
				});
				setTimeout(function() {
					state.client.write("hello");
					Helper.eventually(100, function() {
						return readable == 1;
					}, done);
				}, 50);
			});

			it("allows reading from the request object", function(done) {
				var state = this.state;
				state.client.write("hello");

				setTimeout(function() {
					var chunk = state.req.read(5);
					assert.ok(!!chunk);
					chunk.toString('utf-8').should.eql('hello');
					done();
				}, 50);
			});

			it("emits a readable event if data was already received before attaching the event listener", function(done) {
				var state = this.state;
				state.client.write("hello");

				setTimeout(function() {
					var readable = 0;
					state.req.on('readable', function() {
						readable++;
					});
					Helper.eventually(100, function() {
						return readable == 1;
					}, done);
				}, 50);
			});

			it("pauses the socket data flow if the request buffer becomes too full", function(done) {
				var state = this.state;
				var i, buf;

				state.client.write("hello");
				buf = new Buffer(1024);
				buf.fill("x");
				for (i = 0; i < 1024; i++) {
					state.client.write(buf);
				}

				setTimeout(function() {
					var len = state.req._readableState.length;
					assert.ok(len > 0);
					
					buf = new Buffer(1024);
					buf.fill("y");
					for (i = 0; i < 1024; i++) {
						state.client.write(buf);
					}
					
					setTimeout(function() {
						state.req._readableState.length.should.equal(len);
						var chunk = state.req.read(7);
						assert.ok(!!chunk);
						chunk.toString('utf-8').should.eql("helloxx");
						done();
					}, 100);
				}, 100);
			});

			it("resumes the socket data flow if the request buffer's size drops to below the high water mark", function(done) {
				var state = this.state;
				var i, buf;

				buf = new Buffer(1024);
				buf.fill("x");
				for (i = 0; i < 1024; i++) {
					state.client.write(buf);
				}

				var len = 0;
				var str = buf.slice(0, 512).toString('utf-8');
				state.req.on('readable', function() {
					var chunk;
					while ((chunk = state.req.read(512)) !== null) {
						chunk.toString('utf-8').should.eql(str);
						len += chunk.length;
					}
				});
				Helper.eventually(100, function() {
					return len == 1024 * 1024;
				}, done);
			})

			it("doesn't emit the end event if the request was never read from", function(done) {
				var state = this.state;
				var finished;
				setTimeout(function() {
					if (!finished) {
						finished = true;
						done();
					}
				}, 50);
				state.req.on('end', function() {
					if (!finished) {
						finished = true;
						assert.fail("unexpected end event");
					}
				});
			});

			it("emits the end event upon reaching the end of the request body", function(done) {
				var state = this.state;
				var i, buf;

				state.client.write("hello");
				buf = new Buffer(1024);
				buf.fill("x");
				for (i = 0; i < 1024; i++) {
					state.client.write(buf);
				}
				state.client.end();

				var len = 0;
				state.req.on('readable', function() {
					while ((buf = state.req.read(512)) !== null) {
						len += buf.length;
					}
				});
				state.req.on('end', function() {
					len.should.equal(1024 * 1024);
					done();
				})
			});

			it("emits the end event when read() encounters EOF", function(done) {
				var state = this.state;
				var finished;
				state.req.on('end', function() {
					if (!finished) {
						finished = true;
						done();
					}
				});
				state.client.end();
				setTimeout(function() {
					state.req.read(10);
				}, 10);
			});
		});
	});
	
	describe("if the request doesn't have a request body", function() {
		beforeEach(function() {
			var state = this.state;
			state.setup = function(headers, callback) {
				if (!callback) {
					callback = headers;
					headers = {};
				}
				state.headers = createHeaders(headers);
				state.createSocket(function(serverSocket, client) {
					state.req = HttplibEmulation.createIncomingMessage(
						state.headers, serverSocket, "");
					callback();
				});
			}
		});

		it("isn't in flowing mode by default", function(done) {
			var state = this.state;
			state.setup(function() {
				assert.strictEqual(state.req._flowing, undefined);
				done();
			});
		});

		it("is set to flowing mode upon calling pause", function(done) {
			var state = this.state;
			state.setup(function() {
				state.req.pause();
				assert.strictEqual(state.req._flowing, false);
				done();
			});
		});

		it("is set to flowing mode upon calling resume", function(done) {
			var state = this.state;
			state.setup(function() {
				state.req.resume();
				assert.strictEqual(state.req._flowing, true);
				done();
			});
		});

		it("is set to flowing mode upon attaching a data event handler", function(done) {
			var state = this.state;
			state.setup(function() {
				state.req.on('data', function(chunk) {});
				assert.strictEqual(state.req._flowing, true);
				done();
			});
		});

		describe("when in flowing mode", function() {
			beforeEach(function(done) {
				var state = this.state;
				state.setup(function() {
					state.req.resume();
					assert.ok(state.req._flowing);
					done();
				});
			});

			it("sends the end event immediately", function(done) {
				var state = this.state;
				var finished;
				setTimeout(function() {
					if (!finished) {
						finished = true;
						assert.fail("end event never sent");
					}
				}, 50);
				state.req.on('end', function() {
					if (!finished) {
						finished = true;
						done();
					}
				});
			});
		});

		describe("when in non-flowing mode", function() {
			beforeEach(function(done) {
				var state = this.state;
				state.setup(function() {
					assert.ok(!state.req._flowing);
					done();
				});
			});

			it("doesn't send the end event if the request was never read from", function(done) {
				var state = this.state;
				var finished;
				setTimeout(function() {
					if (!finished) {
						finished = true;
						done();
					}
				}, 50);
				state.req.on('end', function() {
					if (!finished) {
						finished = true;
						assert.fail("unexpected end event");
					}
				});
			});

			it("sends the end event when read() encounters EOF", function(done) {
				var state = this.state;
				var finished;
				state.req.on('end', function() {
					if (!finished) {
						finished = true;
						done();
					}
				});
				state.req.read(10);
			});
		});
	});

	describe('requests with Upgrade header', function() {
		beforeEach(function() {
			var state = this.state;
			state.setup = function(headers, callback) {
				if (!callback) {
					callback = headers;
					headers = {
						'HTTP_UPGRADE': 'websocket'
					};
				}
				state.headers = createHeaders(headers);
				state.createSocket(function(serverSocket, client) {
					state.req = HttplibEmulation.createIncomingMessage(
						state.headers, serverSocket, "");
					callback();
				});
			}
		});

		specify('the request object emits no data events', function(done) {
			var state = this.state;
			state.setup(function() {
				var hasData = false;

				state.req.on('data', function(data) {
					hasData = true;
				});
				state.client.write("hello");

				Helper.shouldNeverHappen(50, function() {
					return hasData;
				}, done);
			});
		});

		specify('the request object ends immediately', function(done) {
			var state = this.state;
			state.setup(function() {
				var readable = false;
				var readData;
				var ended = false;

				state.req.on('readable', function() {
					readable = true;
					readData = state.req.read(100);
				});
				state.req.on('end', function() {
					ended = true;
				});

				Helper.eventually(50, function() {
					return readable && ended;
				}, function() {
					assert.strictEqual(readData, null);
					done();
				});
			});
		});

		specify('the socket emits data events as data is received', function(done) {
			var state = this.state;
			state.setup(function() {
				var hasData = false;

				state.req.socket.on('data', function(data) {
					hasData = true;
				});
				state.client.write("hello");

				Helper.eventually(50, function() {
					return hasData;
				}, done);
			});
		});

		it('allows reading from the socket', function(done) {
			var state = this.state;
			state.setup(function() {
				state.req.socket.on('readable', function() {
					var chunk = state.req.socket.read(5).toString('utf-8');
					chunk.should.eql("hello");
					done();
				});
				state.client.write("hello");
			});
		});
	});
});
