/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2013-2014 Phusion
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

/**
 * Provides helper functions for emulating the http.Server API.
 */

var http = require('http');

const HTTP_HEADERS_WITHOUT_PREFIX = {
	'CONTENT_LENGTH': true,
	'CONTENT_TYPE': true
};

function cgiKeyToHttpHeader(key) {
	if (HTTP_HEADERS_WITHOUT_PREFIX[key]) {
		return key.toLowerCase().replace(/_/g, '-');
	} else if (key.match(/^HTTP_/)) {
		return key.replace(/^HTTP_/, '').toLowerCase().replace(/_/g, '-');
	} else {
		return undefined;
	}
}

function setHttpHeaders(httpHeaders, cgiHeaders) {
	for (var i = 0; i < cgiHeaders.keys.length; i++) {
		var key = cgiHeaders.keys[i];
		var httpHeader = cgiKeyToHttpHeader(key);
		if (httpHeader !== undefined) {
			httpHeaders[httpHeader] = cgiHeaders[key];
		}
	}

	if (cgiHeaders['HTTPS']) {
		httpHeaders['x-forwarded-proto'] = 'https';
	}
	if (!httpHeaders['x-forwarded-for']) {
		httpHeaders['x-forwarded-for'] = cgiHeaders['REMOTE_ADDR'];
	}
}

function inferHttpVersion(protocolDescription) {
	var match = protocolDescription.match(/^HTTP\/(.+)/);
	if (match) {
		return match[1];
	}
}

function createIncomingMessage(headers, socket, bodyBegin) {
	/* Node's HTTP parser simulates an 'end' event if it determines that
	 * the request should not have a request body. Currently (Node 0.10.18),
	 * it thinks GET requests without an Upgrade header should not have a
	 * request body, even though technically such GET requests are allowed
	 * to have a request body. For compatibility reasons we implement the
	 * same behavior as Node's HTTP parser.
	 */

	var message = new http.IncomingMessage(socket);
	setHttpHeaders(message.headers, headers);
	message.cgiHeaders = headers;
	message.httpVersion = inferHttpVersion(headers['SERVER_PROTOCOL']);
	message.method = headers['REQUEST_METHOD'];
	message.url    = headers['REQUEST_URI'];
	message.connection.remoteAddress = headers['REMOTE_ADDR'];
	message.connection.remotePort = parseInt(headers['REMOTE_PORT']);
	message.upgrade = !!headers['HTTP_UPGRADE'];

	if (message.upgrade) {
		// Emit end event as described above.
		message.push(null);
		return message;
	}

	message._emitEndEvent = IncomingMessage_emitEndEvent;
	resetIncomingMessageOverridedMethods(message);

	socket.on('end', function() {
		message._emitEndEvent();
	});
	socket.on('drain', function() {
		message.emit('drain');
	});
	socket.on('timeout', function() {
		message.emit('timeout');
	});

	if (headers['REQUEST_METHOD'] != 'GET') {
		if (bodyBegin.length > 0) {
			message.push(bodyBegin);
		}
		socket.ondata = function(buffer, offset, end) {
			if (!message.push(buffer.slice(offset, end))) {
				socket._handle.readStop();
			}
		}
	} else {
		// Emit end event as described above.
		message.push(null);
	}

	return message;
}

function IncomingMessage_pause() {
	this._flowing = false;
	this._orig_pause();
	resetIncomingMessageOverridedMethods(this);
}

function IncomingMessage_resume() {
	this._flowing = true;
	this._orig_resume();
	resetIncomingMessageOverridedMethods(this);
}

function IncomingMessage_on(event, listener) {
	if (event == 'data') {
		this._flowing = true;
		installDataEventHandler(this);
	} else if (event == 'readable') {
		installReadableEventHandler(this);
	}
	this._orig_on.call(this, event, listener);
	resetIncomingMessageOverridedMethods(this);
	return this;
}

function IncomingMessage_emitEndEvent() {
	if (!this._readableState.endEmitted) {
		this._readableState.endEmitted = true;
		this.emit('end');
	}
}

/*
 * Calling on(), pause() etc on the message object may cause our overrided
 * methods to be set to something else. This is probably becaused by the code
 * in Node.js responsible for switching a stream to flowing mode, e.g.
 * emitDataEvents() in _stream_readable.js. Thus, this function
 * should be called from on(), pause() etc.
 */
function resetIncomingMessageOverridedMethods(message) {
	if (message.pause !== IncomingMessage_pause) {
		message._orig_pause = message.pause;
		message.pause = IncomingMessage_pause;
	}
	if (message.resume !== IncomingMessage_resume) {
		message._orig_resume = message.resume;
		message.resume = IncomingMessage_resume;
	}
	if (message.on !== IncomingMessage_on) {
		message._orig_on = message.on;
		message.on = IncomingMessage_on;
		message.addListener = IncomingMessage_on;
	}
}

function installDataEventHandler(message) {
	if (!message._dataEventHandlerInstalled) {
		message._dataEventHandlerInstalled = true;
		message.socket.on('data', function(chunk) {
			message.emit('data', chunk);
		});
	}
}

function installReadableEventHandler(message) {
	if (!message._readableEventHandlerInstalled) {
		message._readableEventHandlerInstalled = true;
		message.socket.on('readable', function() {
			message.emit('readable');
		});
	}
}

function createServerResponse(req) {
	var res = new http.ServerResponse(req);
	res.assignSocket(req.socket);
	res.shouldKeepAlive = false;
	req.socket.on('drain', function() {
		res.emit('drain');
	});
	req.socket.on('timeout', function() {
		res.emit('timeout');
	});
	res.once('finish', function() {
		req.socket.destroySoon();
	});
	return res;
}

exports.createIncomingMessage = createIncomingMessage;
exports.createServerResponse  = createServerResponse;
