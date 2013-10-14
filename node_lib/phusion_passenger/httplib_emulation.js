/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2013 Phusion
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

function mayHaveRequestBody(headers) {
	return headers['REQUEST_METHOD'] != 'GET' || headers['HTTP_UPGRADE'];
}

function createIncomingMessage(headers, socket, bodyBegin) {
	var message = new http.IncomingMessage(socket);
	setHttpHeaders(message.headers, headers);
	message.cgiHeaders = headers;
	message.httpVersion = inferHttpVersion(headers['SERVER_PROTOCOL']);
	message.method = headers['REQUEST_METHOD'];
	message.url    = headers['REQUEST_URI'];
	message.connection.remoteAddress = headers['REMOTE_ADDR'];
	message.connection.remotePort = parseInt(headers['REMOTE_PORT']);

	function onSocketData(chunk) {
		message.emit('data', chunk);
	}

	function onSocketEnd() {
		message.emit('end');
	}

	socket.on('drain', function() {
		message.emit('drain');
	});
	socket.on('timeout', function() {
		message.emit('timeout');
	});

	/* Node's HTTP parser simulates an 'end' event if it determines that
	 * the request should not have a request body. Currently (Node 0.10.18),
	 * it thinks GET requests without an Upgrade header should not have a
	 * request body, even though technically such GET requests are allowed
	 * to have a request body. For compatibility reasons we implement the
	 * same behavior as Node's HTTP parser.
	 */
	if (mayHaveRequestBody(headers)) {
		socket.on('data', onSocketData);
		socket.on('end', onSocketEnd);
		if (bodyBegin.length > 0) {
			process.nextTick(function() {
				// TODO: we should check here whether the socket hasn't already been closed
				socket.emit('data', bodyBegin);
			});
		}
	} else {
		// TODO: we should check in the next tick whether the socket hasn't already been closed
		process.nextTick(onSocketEnd);
	}

	return message;
}

function createServerResponse(req) {
	var res = new http.ServerResponse(req);
	res.assignSocket(req.socket);
	res.shouldKeepAlive = false;
	res.once('finish', function() {
		req.socket.destroySoon();
	});
	return res;
}

exports.createIncomingMessage = createIncomingMessage;
exports.createServerResponse  = createServerResponse;
