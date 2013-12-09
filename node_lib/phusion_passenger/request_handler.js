/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2012-2013 Phusion
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

var net = require('net');
var SessionProtocolParser = require('./session_protocol_parser').SessionProtocolParser;

/**
 * Handles incoming Phusion Passenger requests and emits events.
 */
function RequestHandler(readyCallback, clientCallback) {
	var self = this;

	function handleNewClient(socket) {
		var state = 'PARSING_HEADER';
		var parser = new SessionProtocolParser();

		function handleReadable() {
			// read(n) returns null unless the buffer is at least n bytes.
			// We just want to read whatever we can we poke into its buffer.
			// Hope they don't change the this.
			var len = socket._readableState.length;
			handleData(socket.read(len));
		}

		function handleData(data) {
			if (state == 'PARSING_HEADER') {
				var consumed = parser.feed(data);
				if (parser.state == SessionProtocolParser.SPP_DONE) {
					state = 'HEADER_SEEN';
					socket.removeListener('readable', handleReadable);
					PhusionPassenger.emit('request', parser, socket, data.slice(consumed));
				} else if (parser.state == SessionProtocolParser.SPP_ERROR) {
					console.error('Header parse error');
					socket.destroySoon();
				}
			} else {
				// Do nothing.
			}
		}

		socket.on('readable', handleReadable);
	}

	var server = net.createServer({ allowHalfOpen: true }, handleNewClient);
	this.server = server;
	server.listen(0, function() {
		readyCallback(self);
	});
}

exports.RequestHandler = RequestHandler;
