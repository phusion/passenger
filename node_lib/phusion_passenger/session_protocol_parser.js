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

const SPP_PARSING_SIZE    = SessionProtocolParser.SPP_PARSING_SIZE = 0;
const SPP_PARSING_HEADERS = SessionProtocolParser.SPP_PARSING_HEADERS = 1;
const SPP_DONE            = SessionProtocolParser.SPP_DONE = 10;
const SPP_ERROR           = SessionProtocolParser.SPP_ERROR = 11;

const ENCODING = 'binary';

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
				key = this.buffer.toString(ENCODING, start, keyEnd);
				value = this.buffer.toString(ENCODING, valueStart, valueEnd);
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

exports.SessionProtocolParser = SessionProtocolParser;
