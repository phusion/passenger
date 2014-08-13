// This file is included inside the RequestHandler class.

private:

HashedStaticString PASSENGER_APP_GROUP_NAME;
HashedStaticString PASSENGER_STICKY_SESSIONS;
HashedStaticString PASSENGER_STICKY_SESSIONS_COOKIE_NAME;
HashedStaticString HTTP_COOKIE;

struct ev_loop *
getLoop() {
	return getContext()->libev->getLoop();
}

void
disconnectWithClientSocketWriteError(Client *client, int e) {
	stringstream message;
	message << "client socket write error: ";
	message << strerror(e);
	message << " (errno=" << e << ")";
	disconnectWithError(&client, message.str());
}

void
disconnectWithAppSocketWriteError(Client *client, int e) {
	stringstream message;
	message << "app socket write error: ";
	message << strerror(e);
	message << " (errno=" << e << ")";
	disconnectWithError(&client, message.str());
}

void
disconnectWithWarning(Client **client, const StaticString &message) {
	SKC_DEBUG(*client, "Disconnected client with warning: " << message);
	disconnect(client);
}

template<typename Number>
static Number
clamp(Number value, Number min, Number max) {
	return std::max(std::min(value, max), min);
}

// `path` MUST be NULL-terminated.
// Returns a contiguous LString.
LString *
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
