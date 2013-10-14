var Helper = require('./spec_helper').Helper;
var FakeStream = require('./spec_helper').FakeStream;
var sinon = require('sinon');
var should = require('should');
var assert = require('assert');
var LineReader = require('phusion_passenger/line_reader').LineReader;

describe('LineReader', function() {
	this.timeout(1000);

	beforeEach(function() {
		this.stream = new FakeStream();
		this.reader = new LineReader(this.stream);
	});

	it('does nothing when the stream is idle', function(done) {
		var finished;
		this.reader.readLine(function(line) {
			if (!finished) {
				finished = true;
				assert.fail();
			}
		});
		setTimeout(function() {
			if (!finished) {
				finished = true;
				done();
			}
		}, 50);
	});

	describe('when one partial line has been received', function() {
		beforeEach(function() {
			this.stream.emit('data', 'hello');
		});

		it('buffers the data', function() {
			this.reader.buffer.should.equal('hello');
		});

		it('resumes the stream', function() {
			this.stream.paused.should.be.false;
			this.reader.paused.should.be.false;
		});

		describe('when the rest of the line is received later', function() {
			beforeEach(function() {
				this.stream.emit('data', " world\n");
			});

			it('memorizes a line', function() {
				this.reader.lines.should.eql(["hello world\n"]);
			});

			it('empties the buffer', function() {
				this.reader.buffer.should.eql('');
			});

			it('pauses the stream', function() {
				this.stream.paused.should.be.true;
				this.reader.paused.should.be.true;
			});
		});

		describe('when the rest of the line, plus a partial line, is received later', function() {
			beforeEach(function() {
				this.stream.emit('data', " world\nhey");
			});

			it('memorizes a line', function() {
				this.reader.lines.should.eql(["hello world\n"]);
			});

			it('buffers the partial line', function() {
				this.reader.buffer.should.eql('hey');
			});

			it('pauses the stream', function() {
				this.stream.paused.should.be.true;
				this.reader.paused.should.be.true;
			});
		});

		describe('when the rest of the line, plus a full line, is received later', function() {
			beforeEach(function() {
				this.stream.emit('data', " world\nhey\n");
			});

			it('memorizes two lines', function() {
				this.reader.lines.should.eql(["hello world\n", "hey\n"]);
			});

			it('empties the buffer', function() {
				this.reader.buffer.should.eql('');
			});

			it('pauses the stream', function() {
				this.stream.paused.should.be.true;
				this.reader.paused.should.be.true;
			});
		});
	});

	describe('when one full line has been received', function() {
		beforeEach(function() {
			this.stream.emit('data', "hello world\n");
		});

		it('memorizes the line', function() {
			this.reader.lines.should.eql(["hello world\n"]);
		});

		it('empties the buffer', function() {
			this.reader.buffer.should.eql('');
		});

		it('pauses the stream', function() {
			this.stream.paused.should.be.true;
			this.reader.paused.should.be.true;
		});
	});

	describe('when multiple full lines have been received', function() {
		beforeEach(function() {
			this.stream.emit('data', "hello world\nhey\n");
		});

		it('memorizes all lines', function() {
			this.reader.lines.should.eql(["hello world\n", "hey\n"]);
		});

		it('empties the buffer', function() {
			this.reader.buffer.should.eql('');
		});

		it('pauses the stream', function() {
			this.stream.paused.should.be.true;
			this.reader.paused.should.be.true;
		});
	});

	describe('when multiple full lines and one partial line have been received', function() {
		beforeEach(function() {
			this.stream.emit('data', "hello world\nhey\nfoo");
		});

		it('memorizes all full lines', function() {
			this.reader.lines.should.eql(["hello world\n", "hey\n"]);
		});

		it('buffers the partial line', function() {
			this.reader.buffer.should.eql('foo');
		});

		it('pauses the stream', function() {
			this.stream.paused.should.be.true;
			this.reader.paused.should.be.true;
		});
	});

	describe('on EOF', function() {
		describe('if the buffer is non-empty', function() {
			beforeEach(function() {
				this.reader.buffer = 'hello';
				this.stream.emit('end');
			});

			it('memorizes the buffer contents as a line', function() {
				this.reader.lines.should.eql(["hello"]);
			});
		});

		it('marks the reader as having reached EOF', function() {
			this.stream.emit('end');
			this.reader.endReached().should.be.true;
		});

		it('pauses the stream', function() {
			this.stream.emit('end');
			this.stream.paused.should.be.true;
			this.reader.paused.should.be.true;
		})
	});

	describe('.readLine', function() {
		describe('if there is at least one line in memory', function() {
			beforeEach(function() {
				this.reader.lines = ["hello\n", "world\n"];
			});

			it('pops the first line in memory', function(done) {
				this.reader.readLine(function(line) {
					line.should.eql("hello\n");
					done();
				});
			});

			describe('if the line memory is non-empty after reading', function() {
				it('pauses the stream', function(done) {
					var self = this;
					this.reader.readLine(function(line) {
						line.should.eql("hello\n");
						process.nextTick(function() {
							self.stream.paused.should.be.true;
							self.reader.paused.should.be.true;
							done();
						});
					});
				});
			});

			describe('if the line memory is empty after reading', function() {
				beforeEach(function() {
					this.reader.lines = ["hello\n"];
				});

				it('resumes the stream', function(done) {
					var self = this;
					this.reader.readLine(function(line) {
						line.should.eql("hello\n");
						process.nextTick(function() {
							self.stream.paused.should.be.false;
							self.reader.paused.should.be.false;
							done();
						});
					});
				});
			});

			describe('if the stream had already reached EOF', function() {
				beforeEach(function() {
					this.reader.eof = true;
					this.reader.lines = ["hello\n"];
				});

				it('yields the line upon first call', function(done) {
					this.reader.readLine(function(line) {
						line.should.eql("hello\n");
						done();
					});
				});

				it('yields undefined once the line memory has become empty', function(done) {
					var self = this;
					this.reader.readLine(function(line) {
						line.should.eql("hello\n");
						self.reader.readLine(function(line2) {
							should(line2).equal(undefined);
							self.reader.readLine(function(line3) {
								should(line3).equal(undefined);
								done();
							});
						});
					});
				});
			});

			describe('when calling readLine again in the callback', function() {
				beforeEach(function() {
					this.reader.lines = ["hello\n"];
				});

				it('waits until another line has been memorized', function(done) {
					var self = this;
					var finished;
					this.reader.readLine(function(line) {
						line.should.eql("hello\n");
						self.reader.readLine(function(line2) {
							if (!finished) {
								finished = true;
								line2.should.eql("world\n");
								done();
							}
						});
						setTimeout(function() {
							if (!finished) {
								finished = true;
								self.stream.emit('data', "world\n");
								done();
							}
						}, 50);
					});
				});

				it('resumes the stream', function(done) {
					var self = this;
					var finished;
					this.reader.readLine(function(line) {
						line.should.eql("hello\n");
						self.reader.readLine(function(line2) {
							if (!finished) {
								finished = true;
								assert.fail("never reached");
							}
						});
						process.nextTick(function() {
							if (!finished) {
								finished = true;
								self.stream.paused.should.be.false;
								self.reader.paused.should.be.false;
								done();
							}
						});
					});
				});
			});
		});

		describe('if there is no line in memory', function() {
			it('waits until a line has been memorized', function(done) {
				var state = 'waiting';
				var self = this;
				this.reader.readLine(function(line) {
					state.should.eql('line fed');
					line.should.eql("hello\n");
					done();
				});
				setTimeout(function() {
					state.should.eql('waiting');
					state = 'line fed';
					self.stream.emit('data', "hello\n");
				}, 50);
			});

			describe('if the stream had already reached EOF', function(done) {
				beforeEach(function() {
					this.reader.eof = true;
				})

				it('yields undefined', function() {
					this.reader.readLine(function(line) {
						should(line).equal(undefined);
					});
				});
			});
		});
	});
});
