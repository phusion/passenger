/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2013 Phusion
 *
 *  "Phusion Passenger" is a trademark of Hongli Lai & Ninh Bui.
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 *  THE SOFTWARE.
 */

var EventEmitter = require('events').EventEmitter;
var net = require('net');
GLOBAL.PhusionPassenger = new EventEmitter();

/**
 * Class for reading a stream line-by-line.
 * Usage:
 *
 * reader = new LineReader(stream);
 * reader.readLine(function(line) {
 *     ...
 *     // When done:
 *     reader.close();
 * });
 */
function LineReader(stream) {
	var self = this;
	this.stream = stream;

	this.buffer = '';
	this.lines = [];
	this.callbacks = [];

	function handleLineBuffer() {
		if (self.lineBufferIsFull()) {
			stream.pause();
			self.paused = true;
		}

		while (self.buffer != undefined && self.lines.length > 0 && self.callbacks.length > 0) {
			line = self.lines.shift();
			callback = self.callbacks.shift();
			callback(line);
		}

		if (self.buffer != undefined && !self.lineBufferIsFull() && self.paused) {
			self.paused = false;
			self.stream.resume();
		}
	}

	function onData(data) {
		var index, line, callback;

		if (self.buffer == undefined) {
			// Already closed.
			return;
		}

		self.buffer += data;
		while ((index = self.buffer.indexOf("\n")) != -1) {
			line = self.buffer.substr(0, index + 1);
			self.buffer = self.buffer.substr(index + 1);
			self.lines.push(line);
		}
		handleLineBuffer();
	}

	function onEnd() {
		if (self.buffer != undefined) {
			self.lines.push(self.buffer);
			self.buffer = '';
			handleLineBuffer();
			if (self.onEof) {
				self.onEof();
			}
		}
	}

	this.onData = onData;
	this.onEnd  = onEnd;
	stream.on('data', onData);
	stream.on('end', onEnd);
	stream.resume();
}

LineReader.prototype.close = function() {
	this.stream.pause();
	this.stream.removeListener('data', this.onData);
	this.stream.removeListener('end', this.onEnd);
	this.buffer = undefined;
	this.lines = undefined;
}

LineReader.prototype.lineBufferIsFull = function() {
	return this.lines.length > 0;
}

LineReader.prototype.readLine = function(callback) {
	if (this.lines.length > 0) {
		var line = this.lines.shift();
		if (!this.lineBufferIsFull() && this.paused) {
			this.paused = false;
			this.stream.resume();
		}
		callback(line);
	} else {
		this.callbacks.push(callback);
	}
}


const
	SPP_PARSING_SIZE = 0,
	SPP_PARSING_HEADERS = 1,
	SPP_DONE = 10,
	SPP_ERROR = 11,
	SPP_ENCODING = 'binary';

function SessionProtocolParser() {
	this.state = SPP_PARSING_SIZE;
	this.processed = 0;
	this.size = 0;
	this.keys = [];
}

SessionProtocolParser.prototype.feed = function(buffer) {
	var consumed = 0;
	var locallyConsumed;

	while (consumed < buffer.length && this.state != SPP_ERROR && this.state != SPP_DONE) {
		switch (this.state) {
		case SPP_PARSING_SIZE:
			this.size += buffer[consumed] * Math.pow(256, 3 - this.processed);
			locallyConsumed = 1;
			this.processed++;
			if (this.processed == 4) {
				this.state = SPP_PARSING_HEADERS;
				this.buffer = new Buffer(this.size);
				this.processed = 0;
			}
			break;

		case SPP_PARSING_HEADERS:
			locallyConsumed = Math.min(buffer.length - consumed, this.buffer.length - this.processed);
			buffer.copy(this.buffer, this.processed, consumed, consumed + locallyConsumed);
			this.processed += locallyConsumed;
			if (this.processed == this.buffer.length) {
				this.state = SPP_DONE;
				this.parse();
			}
			break;

		default:
			console.assert(false);
			break;
		}

		consumed += locallyConsumed;
	}

	return consumed;
}

SessionProtocolParser.prototype.parse = function() {
	function findZero(buffer, start) {
		while (start < buffer.length) {
			if (buffer[start] == 0) {
				return start;
			} else {
				start++;
			}
		}
		return -1;
	}

	var start = 0;
	var key, value;

	while (start < this.buffer.length) {
		var keyEnd = findZero(this.buffer, start);
		if (keyEnd != -1 && keyEnd + 1 < this.buffer.length) {
			var valueStart = keyEnd + 1;
			var valueEnd   = findZero(this.buffer, valueStart);
			if (valueEnd != -1) {
				key = this.buffer.toString(SPP_ENCODING, start, keyEnd);
				value = this.buffer.toString(SPP_ENCODING, valueStart, valueEnd);
				start = valueEnd + 1;
				this.keys.push(key);
				this[key] = value;
			} else {
				start = this.buffer.length;
			}
		} else {
			start = this.buffer.length;
		}
	}
}

//var p = new SessionProtocolParser();
//p.feed(new Buffer("\x00\x00\x00\x06hi\0ho\0"));
//process.exit();


function RequestHandler(readyCallback, clientCallback) {
	var self = this;

	function handleNewClient(socket) {
		var state = 'PARSING_HEADER';
		var parser = new SessionProtocolParser();

		function handleData(data) {
			if (state == 'PARSING_HEADER') {
				var consumed = parser.feed(data);
				if (parser.state == SPP_DONE) {
					state = 'HEADER_SEEN';
					socket.removeListener('data', handleData);
					PhusionPassenger.emit('request', parser, socket);
					if (consumed != data.length) {
						socket.emit('data', data.slice(consumed));
					}
				} else if (parser.state == SPP_ERROR) {
					console.error('Header parse error');
					socket.destroySoon();
				}
			} else {
				// Do nothing.
			}
		}

		socket.on('data', handleData);
	}

	var server = net.createServer({ allowHalfOpen: true }, handleNewClient);
	this.server = server;
	server.listen(0, function() {
		readyCallback(self);
	});
}


/**************************/

var reader = new LineReader(process.stdin);

function readInitializationHeader() {
	reader.readLine(function(line) {
		if (line != "You have control 1.0\n") {
			console.error('Invalid initialization header');
			process.exit(1);
		} else {
			readOptions();
		}
	});
}

function readOptions() {
	var options = {};

	function readNextOption() {
		reader.readLine(function(line) {
			if (line == "\n") {
				initialize(options);
			} else if (line == "") {
				console.error("End of stream encountered while reading initialization options");
				process.exit(1);
			} else {
				var matches = line.replace(/\n/, '').match(/(.*?) *: *(.*)/);
				options[matches[1]] = matches[2];
				readNextOption();
			}
		});
	}

	readNextOption();
}

function initialize(options) {
	PhusionPassenger.options = options;

	PhusionPassenger.requestHandler = new RequestHandler(function() {
		require(options.app_root + '/passenger_node.js');
		process.stdout.write("!> Ready\n");
		process.stdout.write("!> socket: main;tcp://127.0.0.1:" +
			PhusionPassenger.requestHandler.server.address().port +
			";session;0\n");
		process.stdout.write("!> \n");
	});

	reader.close();
	reader = undefined;
	process.stdin.on('end', function() {
		if (PhusionPassenger.listeners('exit').length == 0) {
			process.exit(0);
		} else {
			PhusionPassenger.emit('exit');
		}
	});
	process.stdin.resume();
}

process.stdout.write("!> I have control 1.0\n");
readInitializationHeader();
