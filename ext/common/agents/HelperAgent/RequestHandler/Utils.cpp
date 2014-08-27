// This file is included inside the RequestHandler class.

private:

struct ev_loop *
getLoop() {
	return getContext()->libev->getLoop();
}

void
disconnectWithClientSocketWriteError(Client **client, int e) {
	stringstream message;
	message << "client socket write error: ";
	message << ServerKit::getErrorDesc(e);
	message << " (errno=" << e << ")";
	disconnectWithError(client, message.str());
}

void
disconnectWithAppSocketIncompleteResponseError(Client **client) {
	disconnectWithError(client, "application did not send a complete response");
}

void
disconnectWithAppSocketReadError(Client **client, int e) {
	stringstream message;
	message << "app socket read error: ";
	message << ServerKit::getErrorDesc(e);
	message << " (errno=" << e << ")";
	disconnectWithError(client, message.str());
}

void
disconnectWithAppSocketWriteError(Client **client, int e) {
	stringstream message;
	message << "app socket write error: ";
	message << ServerKit::getErrorDesc(e);
	message << " (errno=" << e << ")";
	disconnectWithError(client, message.str());
}

void
endRequestWithAppSocketIncompleteResponse(Client **client, Request **req) {
	if (!(*req)->responseBegun) {
		SKC_WARN(*client, "Sending 502 response: application did not send a complete response");
		endRequestWithSimpleResponse(client, req,
			"<h2>Incomplete response received from application</h2>", 502);
	} else {
		disconnectWithAppSocketIncompleteResponseError(client);
	}
}

void
endRequestWithAppSocketReadError(Client **client, Request **req, int e) {
	Client *c = *client;
	if (!(*req)->responseBegun) {
		SKC_WARN(*client, "Sending 502 response: application socket read error");
		endRequestWithSimpleResponse(client, req, "<h2>Application socket read error</h2>", 502);
	} else {
		disconnectWithAppSocketReadError(&c, e);
	}
}

/**
 * `data` must outlive the request.
 */
void
endRequestWithSimpleResponse(Client **c, Request **r, const StaticString &body, int code = 200) {
	Client *client = *c;
	Request *req = *r;
	ServerKit::HeaderTable headers;

	headers.insert(req->pool, "cache-control", "no-cache, no-store, must-revalidate");
	writeSimpleResponse(client, code, &headers, body);
	endRequest(c, r);
}

bool
getBoolOption(Request *req, const HashedStaticString &name, bool defaultValue = false) {
	const LString *value = req->secureHeaders.lookup(name);
	if (value != NULL && value->size > 0) {
		return psg_lstr_first_byte(value) == 't';
	} else {
		return defaultValue;
	}
}

template<typename Number>
static Number
clamp(Number value, Number min, Number max) {
	return std::max(std::min(value, max), min);
}

static void
gatherBuffers(char * restrict dest, unsigned int size, const struct iovec *buffers,
	unsigned int nbuffers)
{
	const char *end = dest + size;
	char *pos = dest;

	for (unsigned int i = 0; i < nbuffers; i++) {
		assert(pos + buffers[i].iov_len <= end);
		memcpy(pos, buffers[i].iov_base, buffers[i].iov_len);
		pos += buffers[i].iov_len;
	}
}

static Json::Value
timeToJson(ev_tstamp tstamp) {
	Json::Value doc;
	time_t time = (time_t) tstamp;
	char buf[32];
	size_t len;

	doc["timestamp"] = tstamp;

	ctime_r(&time, buf);
	len = strlen(buf);
	if (len > 0) {
		// Get rid of trailing newline
		buf[len - 1] = '\0';
	}
	doc["local"] = buf;

	return doc;
}

// `path` MUST be NULL-terminated. Returns a contiguous LString.
static LString *
resolveSymlink(const StaticString &path, psg_pool_t *pool) {
	char linkbuf[PATH_MAX];
	ssize_t size;

	size = readlink(path.data(), linkbuf, sizeof(linkbuf) - 1);
	if (size == -1) {
		if (errno == EINVAL) {
			return psg_lstr_create(pool, path);
		} else {
			int e = errno;
			string message = "Cannot resolve possible symlink '";
			message.append(path.data(), path.size());
			message.append("'");
			throw FileSystemException(message, e, path.data());
		}
	} else {
		linkbuf[size] = '\0';
		if (linkbuf[0] == '\0') {
			string message = "The file '";
			message.append(path.data(), path.size());
			message.append("' is a symlink, and it refers to an empty filename. This is not allowed.");
			throw FileSystemException(message, ENOENT, path.data());
		} else if (linkbuf[0] == '/') {
			// Symlink points to an absolute path.
			return psg_lstr_create(pool, linkbuf, strlen(linkbuf));
		} else {
			// Symlink points to a relative path.
			// We do not use absolutizePath() because it's too slow.
			// This version doesn't handle all the edge cases but is
			// much faster.
			StaticString workingDir = extractDirNameStatic(path);
			size_t linkbuflen = strlen(linkbuf);
			size_t resultlen = linkbuflen + 1 + workingDir.size();
			char *data = (char *) psg_pnalloc(pool, resultlen);
			char *pos  = data;
			char *end  = data + resultlen;

			pos = appendData(pos, end, linkbuf, linkbuflen);
			*pos = '/';
			pos++;
			pos = appendData(pos, end, workingDir);

			return psg_lstr_create(pool, data, resultlen);
		}
	}
}

void
parseCookieHeader(psg_pool_t *pool, const LString *headerValue,
	vector< pair<StaticString, StaticString> > &cookies) const
{
	// See http://stackoverflow.com/questions/6108207/definite-guide-to-valid-cookie-values
	// for syntax grammar.
	vector<StaticString> parts;
	vector<StaticString>::const_iterator it, it_end;

	headerValue = psg_lstr_make_contiguous(headerValue, pool);
	split(StaticString(headerValue->start->data, headerValue->size),
		';', parts);
	cookies.reserve(parts.size());
	it_end = parts.end();

	for (it = parts.begin(); it != it_end; it++) {
		const char *begin = it->data();
		const char *end = it->data() + it->size();
		const char *sep;

		skipLeadingWhitespaces(&begin, end);
		skipTrailingWhitespaces(begin, &end);

		// Find the separator ('=').
		sep = (const char *) memchr(begin, '=', end - begin);
		if (sep != NULL) {
			// Valid cookie. Otherwise, ignore it.
			const char *nameEnd = sep;
			const char *valueBegin = sep + 1;

			skipTrailingWhitespaces(begin, &nameEnd);
			skipLeadingWhitespaces(&valueBegin, end);

			cookies.push_back(make_pair(
				StaticString(begin, nameEnd - begin),
				StaticString(valueBegin, end - valueBegin)
			));
		}
	}
}
