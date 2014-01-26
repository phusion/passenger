var fs = require('fs');
var url = require('url');
var express = require('express');
var app = express();
var bodyParser = express.bodyParser();

function textResponse(res, content) {
	content = String(content);
	res.setHeader("Content-Type", "text/plain");
	res.setHeader("Content-Length", content.length);
	res.end(content);
}

function fileExists(filename) {
	try {
		fs.statSync(filename);
		return true;
	} catch (e) {
		return false;
	}
}

if (process.env.PASSENGER_BASE_URI) {
	app.use(process.env.PASSENGER_BASE_URI, app.router);
}

app.all('/', function(req, res) {
	if (fileExists("front_page.txt")) {
		textResponse(res, fs.readFileSync("front_page.txt"));
	} else {
		textResponse(res, "front page");
	}
});

app.all('/parameters', function(req, res) {
	bodyParser(req, res, function() {
		var first = req.query.first || req.body.first;
		var second = req.query.second || req.body.second;
		textResponse(res, "Method: " + req.method + "\n" +
			"First: " + first + "\n" +
			"Second: " + second + "\n")
	});
});

app.all('/chunked', function(req, res) {
	res.setHeader("Content-Type", "text/plain");
	res.setHeader("Transfer-Encoding", "chunked");
	res.write("chunk1\n");
	res.write("chunk2\n");
	res.write("chunk3\n");
	res.end();
});

app.all('/pid', function(req, res) {
	textResponse(res, process.pid);
});

app.all(/^\/env/, function(req, res) {
	var body = '';
	var keys = [];
	for (var key in req.cgiHeaders) {
		keys.push(key);
	}
	keys.sort();
	for (var i = 0; i < keys.length; i++) {
		var val = req.cgiHeaders[keys[i]];
		if (val === undefined) {
			val = '';
		}
		body += keys[i] + " = " + val + "\n";
	}
	textResponse(res, body);
});

app.all('/touch_file', function(req, res) {
	bodyParser(req, res, function() {
		var filename = req.query.file || req.body.file;
		fs.writeFileSync(filename, "");
		textResponse(res, "ok")
	});
});

app.all('/extra_header', function(req, res) {
	res.setHeader("Content-Type", "text/html");
	res.setHeader("Content-Length", "2");
	res.setHeader("X-Foo", "Bar");
	res.end("ok");
});

app.all('/cached', function(req, res) {
	textResponse(res, "This is the uncached version of /cached");
});

app.all('/upload_with_params', function(req, res) {
	bodyParser(req, res, function() {
		var name1 = req.query.name1 || req.body.name1;
		var name2 = req.query.name2 || req.body.name2;
		var data = fs.readFileSync(req.files.data.path);
		var body =
			"name 1 = " + name1 + "\n" +
			"name 2 = " + name2 + "\n" +
			"data = ";
		var bodyBuffer = new Buffer(body);
		res.setHeader("Content-Type", "text/plain");
		res.setHeader("Content-Length", bodyBuffer.length + data.length);
		res.write(bodyBuffer);
		res.write(data);
		res.end();
	});
});

app.all('/raw_upload_to_file', function(req, res) {
	var filename = req.headers['x-output'];
	var stream = fs.createWriteStream(filename);
	req.on('data', function(data) {
		stream.write(data);
	});
	req.on('end', function() {
		stream.end();
		textResponse(res, "ok");
	});
});

app.listen(3000);
