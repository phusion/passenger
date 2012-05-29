/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (c) 2012 Phusion
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


function RequestHandler(readyCallback, clientCallback) {
	var self = this;

	function handleNewClient(socket) {
		function handleData(data) {

		}

		socket.on('data', handleData);
		process.passenger.emit('request', socket, socket);
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
	var passenger = process.passenger = new EventEmitter();
	passenger.options = options;

	passenger.requestHandler = new RequestHandler(function() {
		require(options.app_root + '/passenger_node.js');
		console.log('!> Ready');
		console.log('!> socket: main;tcp://127.0.0.1:' + passenger.requestHandler.server.address().port + ';session;0');
		console.log('!> ');
	});

	reader.close();
	reader = undefined;
	process.stdin.on('data', function() {
		console.error('##### Stdin data!');
	});
	process.stdin.on('end', function() {
		console.error('##### Stdin end!');
		if (passenger.listeners('exit').length == 0) {
			process.exit(0);
		} else {
			passenger.emit('exit');
		}
	});
	setInterval(function() {
		console.log('####### stdout ping!');
		console.error('####### stderr ping!');
	}, 1000);
	process.stdin.resume();
}

console.log('!> I have control 1.0');
readInitializationHeader();
