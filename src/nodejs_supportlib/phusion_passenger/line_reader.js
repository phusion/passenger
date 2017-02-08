/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2012-2017 Phusion Holding B.V.
 *
 *  "Passenger", "Phusion Passenger" and "Union Station" are registered
 *  trademarks of Phusion Holding B.V.
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
	this.paused = false;

	function handleLineBuffer() {
		var line, callback;

		if (self.lineBufferIsFull()) {
			stream.pause();
			self.paused = true;
		}

		while (self.buffer != undefined && self.lines.length > 0 && self.callbacks.length > 0) {
			line = self.lines.shift();
			callback = self.callbacks.shift();
			callback(line);
		}

		self._autoPauseOrResume();
	}

	function onData(data) {
		var index, line;

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
			self.eof = true;
			handleLineBuffer();
		}
	}

	this.onData = onData;
	this.onEnd  = onEnd;
	stream.on('data', onData);
	stream.on('end', onEnd);
	stream.resume();
}

LineReader.prototype.close = function() {
	this.stream.removeListener('data', this.onData);
	this.stream.removeListener('end', this.onEnd);
	this._pause();
	this.buffer = undefined;
	this.lines = undefined;
};

LineReader.prototype.isClosed = function() {
	return this.buffer === undefined;
};

LineReader.prototype.endReached = function() {
	return this.eof;
};

LineReader.prototype.lineBufferIsFull = function() {
	return this.lines.length > 0;
};

LineReader.prototype.readLine = function(callback) {
	if (this.lines.length > 0) {
		var line = this.lines.shift();
		if (!this.lineBufferIsFull() && this.paused) {
			this.paused = false;
			this.stream.resume();
		}
		callback(line);
		this._autoPauseOrResume();
	} else if (this.eof) {
		callback(undefined);
	} else {
		this.callbacks.push(callback);
	}
};

LineReader.prototype._autoPauseOrResume = function() {
	if (this.buffer != undefined) {
		if (this.lineBufferIsFull() || this.eof) {
			this._pause();
		} else {
			this._resume();
		}
	}
};

LineReader.prototype._pause = function() {
	if (!this.paused) {
		this.paused = true;
		this.stream.pause();
	}
};

LineReader.prototype._resume = function() {
	if (this.paused) {
		this.paused = false;
		this.stream.resume();
	}
};

exports.LineReader = LineReader;
