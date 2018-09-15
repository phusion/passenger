var fs = require('fs');
var url = require('url');
var express = require('express');
var bodyParser = require('body-parser');
var multer = require('multer');
var app = express();

var urlencodedParser = bodyParser.urlencoded({ extended: false });
var upload = multer({ storage: multer.memoryStorage() });
var router = express.Router();

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

router.route('/').all(function(req, res) {
	if (fileExists("front_page.txt")) {
		textResponse(res, fs.readFileSync("front_page.txt"));
	} else {
		textResponse(res, "front page");
	}
});

router.route('/parameters').all(urlencodedParser, function(req, res) {
	var first = req.query.first || req.body.first;
	var second = req.query.second || req.body.second;
	textResponse(res, "Method: " + req.method + "\n" +
		"First: " + first + "\n" +
		"Second: " + second + "\n")
});

router.route('/chunked').all(function(req, res) {
	res.setHeader("Content-Type", "text/plain");
	res.setHeader("Transfer-Encoding", "chunked");
	res.write("chunk1\n");
	res.write("chunk2\n");
	res.write("chunk3\n");
	res.end();
});

router.route('/pid').all(function(req, res) {
	textResponse(res, process.pid);
});

router.route(/^\/env/).all(function(req, res) {
	var result = [];
	var requestUri = req.url;
	var urlParts = url.parse(req.url);

	if (process.env.PASSENGER_BASE_URI) {
		requestUri = process.env.PASSENGER_BASE_URI + requestUri;
		result.push('SCRIPT_NAME = ' + process.env.PASSENGER_BASE_URI);
	} else {
		result.push('SCRIPT_NAME = ');
	}

	result.push('REQUEST_URI = ' + requestUri);
	result.push('PATH_INFO = ' + req.path);
	result.push('QUERY_STRING = ' + (urlParts.query || ''));

	for (var key in req.headers) {
		result.push('HTTP_' + key.toUpperCase().replace(/-/g, '_')
			+ ' = ' + req.headers[key]);
	}

	result.sort();
	textResponse(res, result.join("\n") + "\n");
});

router.route('/touch_file').all(urlencodedParser, function(req, res) {
	var filename = req.query.file || req.body.file;
	fs.writeFileSync(filename, "");
	textResponse(res, "ok")
});

router.route('/extra_header').all(function(req, res) {
	res.setHeader("Content-Type", "text/html");
	res.setHeader("Content-Length", "2");
	res.setHeader("X-Foo", "Bar");
	res.end("ok");
});

router.route('/cached').all(function(req, res) {
	textResponse(res, "This is the uncached version of /cached");
});

router.route('/upload_with_params').all(
	urlencodedParser,
	upload.single('data'),
	function(req, res)
{
	var name1 = req.query.name1 || req.body.name1;
	var name2 = req.query.name2 || req.body.name2;
	var data = req.body.data ? new Buffer(req.body.data) : req.file.buffer;
	var preamble = new Buffer(
		"name 1 = " + name1 + "\n" +
		"name 2 = " + name2 + "\n" +
		"data = ");

	res.setHeader("Content-Type", "text/plain");
	res.setHeader("Transfer-Encoding", "chunked");
	res.write(preamble);
	res.write(data);
	res.end();
});

router.route('/raw_upload_to_file').all(function(req, res) {
	var filename = req.headers['x-output'];
	var stream = fs.createWriteStream(filename);
	req.on('data', function(data) {
		stream.write(data);
	});
	req.on('end', function() {
		stream.end(function() {
			textResponse(res, "ok");
		});
	});
});

app.use(process.env.PASSENGER_BASE_URI || '/', router);
app.listen(process.env.PORT || 3000);
