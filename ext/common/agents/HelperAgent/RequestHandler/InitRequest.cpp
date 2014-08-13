// This file is included inside the RequestHandler class.
// It handles main events, and may forward events to
// respective state-specific handlers.

protected:

virtual void
onRequestBegin(Client *client, Request *req) {
	req->startedAt = ev_now(getLoop());

	initializePoolOptions(client, req);
	if (req->ended()) {
		return;
	}
	initializeUnionStation(client, req);
	if (req->ended()) {
		return;
	}
	setStickySessionId(client, req);

	//checkoutSession(client, req);

	writeResponse(client,
		"HTTP/1.1 200 OK\r\n"
		"Content-Length: 3\r\n"
		"Content-Type: text/plain\r\n"
		"Connection: keep-alive\r\n"
		"\r\n"
		"ok\n"
	);
	endRequest(&client, &req);
}

private:

void
initializePoolOptions(Client *client, Request *req) {
	boost::shared_ptr<Options> *options;

	if (singleAppMode) {
		assert(poolOptionsCache.size() == 1);
		poolOptionsCache.lookupRandom(NULL, &options);
		req->options = **options;
	} else {
		ServerKit::HeaderTable::Cell *appGroupNameCell =
			req->secureHeaders.lookupCell(PASSENGER_APP_GROUP_NAME);
		if (appGroupNameCell != NULL && appGroupNameCell->header->val.size > 0) {
			const LString *appGroupName = psg_lstr_make_contiguous(
				&appGroupNameCell->header->val,
				req->pool);

			poolOptionsCache.lookup(HashedStaticString(appGroupName->start->data,
				appGroupName->size), &options);

			if (options != NULL) {
				req->options = **options;
			} else {
				createNewPoolOptions(client, req);
			}
		} else {
			disconnectWithError(&client, "the !~PASSENGER_APP_GROUP_NAME header must be set");
			return;
		}
	}
}

static void
fillPoolOption(Request *req, StaticString &field, const HashedStaticString &name) {
	const LString *value = req->secureHeaders.lookup(name);
	if (value != NULL) {
		value = psg_lstr_make_contiguous(value, req->pool);
		field = StaticString(value->start->data, value->size);
	}
}

static void
fillPoolOption(Request *req, bool &field, const HashedStaticString &name) {
	const LString *value = req->secureHeaders.lookup(name);
	if (value != NULL && value->size > 0) {
		field = psg_lstr_first_byte(value) == 't';
	}
}

static void
fillPoolOption(Request *req, unsigned int &field, const HashedStaticString &name) {
	const LString *value = req->secureHeaders.lookup(name);
	if (value != NULL && value->size > 0) {
		value = psg_lstr_make_contiguous(value, req->pool);
		field = stringToUint(StaticString(value->start->data, value->size));
	}
}

static void
fillPoolOption(Request *req, unsigned long &field, const HashedStaticString &name) {
	const LString *value = req->secureHeaders.lookup(name);
	if (value != NULL && value->size > 0) {
		value = psg_lstr_make_contiguous(value, req->pool);
		field = stringToUint(StaticString(value->start->data, value->size));
	}
}

static void
fillPoolOption(Request *req, long &field, const HashedStaticString &name) {
	const LString *value = req->secureHeaders.lookup(name);
	if (value != NULL && value->size > 0) {
		value = psg_lstr_make_contiguous(value, req->pool);
		field = stringToInt(StaticString(value->start->data, value->size));
	}
}

static void
fillPoolOptionSecToMsec(Request *req, unsigned int &field, const HashedStaticString &name) {
	const LString *value = req->secureHeaders.lookup(name);
	if (value != NULL && value->size > 0) {
		value = psg_lstr_make_contiguous(value, req->pool);
		field = stringToInt(StaticString(value->start->data, value->size)) * 1000;
	}
}

void
createNewPoolOptions(Client *client, Request *req) {
	ServerKit::HeaderTable &secureHeaders = req->secureHeaders;
	Options &options = req->options;

	options = Options();

	const LString *scriptName = secureHeaders.lookup("!~SCRIPT_NAME");
	const LString *appRoot = secureHeaders.lookup("!~PASSENGER_APP_ROOT");
	if (scriptName == NULL || scriptName->size == 0) {
		if (appRoot == NULL || appRoot->size == 0) {
			const LString *documentRoot = secureHeaders.lookup("!~DOCUMENT_ROOT");
			if (OXT_UNLIKELY(documentRoot == NULL || documentRoot->size == 0)) {
				disconnectWithError(&client, "client did not send a !~PASSENGER_APP_ROOT or a !~DOCUMENT_ROOT header");
				return;
			}

			documentRoot = psg_lstr_make_contiguous(documentRoot, req->pool);
			appRoot = psg_lstr_create(req->pool,
				extractDirNameStatic(StaticString(documentRoot->start->data,
					documentRoot->size)));
		} else {
			appRoot = psg_lstr_make_contiguous(appRoot, req->pool);
		}
		options.appRoot = StaticString(appRoot->start->data, appRoot->size);
	} else {
		if (appRoot == NULL || appRoot->size == 0) {
			const LString *documentRoot = secureHeaders.lookup("!~DOCUMENT_ROOT");
			if (OXT_UNLIKELY(documentRoot == NULL || documentRoot->size == 0)) {
				disconnectWithError(&client, "client did not send a !~DOCUMENT_ROOT header");
				return;
			}

			documentRoot = psg_lstr_null_terminate(documentRoot, req->pool);
			documentRoot = resolveSymlink(StaticString(documentRoot->start->data,
				documentRoot->size), req->pool);
			appRoot = psg_lstr_create(req->pool,
				extractDirNameStatic(StaticString(documentRoot->start->data,
					documentRoot->size)));
		} else {
			appRoot = psg_lstr_make_contiguous(appRoot, req->pool);
		}
		options.appRoot = StaticString(appRoot->start->data, appRoot->size);
		scriptName = psg_lstr_make_contiguous(scriptName, req->pool);
		options.baseURI = StaticString(scriptName->start->data, scriptName->size);
	}

	options.ruby = this->options.defaultRubyCommand;
	options.logLevel = getLogLevel();
	options.loggingAgentAddress = this->options.loggingAgentAddress;
	options.loggingAgentUsername = P_STATIC_STRING("logging");
	options.loggingAgentPassword = this->options.loggingAgentPassword;
	options.defaultUser = this->options.defaultUser;
	options.defaultGroup = this->options.defaultGroup;

	fillPoolOption(req, options.appGroupName, "!~PASSENGER_APP_GROUP_NAME");
	fillPoolOption(req, options.appType, "!~PASSENGER_APP_TYPE");
	fillPoolOption(req, options.environment, "!~PASSENGER_APP_ENV");
	fillPoolOption(req, options.ruby, "!~PASSENGER_RUBY");
	fillPoolOption(req, options.python, "!~PASSENGER_PYTHON");
	fillPoolOption(req, options.nodejs, "!~PASSENGER_NODEJS");
	fillPoolOption(req, options.user, "!~PASSENGER_USER");
	fillPoolOption(req, options.group, "!~PASSENGER_GROUP");
	fillPoolOption(req, options.minProcesses, "!~PASSENGER_MIN_PROCESSES");
	fillPoolOption(req, options.maxProcesses, "!~PASSENGER_MAX_PROCESSES");
	fillPoolOption(req, options.maxRequests, "!~PASSENGER_MAX_REQUESTS");
	fillPoolOption(req, options.spawnMethod, "!~PASSENGER_SPAWN_METHOD");
	fillPoolOption(req, options.startCommand, "!~PASSENGER_START_COMMAND");
	fillPoolOptionSecToMsec(req, options.startTimeout, "!~PASSENGER_START_TIMEOUT");
	fillPoolOption(req, options.maxPreloaderIdleTime, "!~PASSENGER_MAX_PRELOADER_IDLE_TIME");
	fillPoolOption(req, options.maxRequestQueueSize, "!~PASSENGER_MAX_REQUEST_QUEUE_SIZE");
	fillPoolOption(req, options.statThrottleRate, "!~PASSENGER_STAT_THROTTLE_RATE");
	fillPoolOption(req, options.restartDir, "!~PASSENGER_RESTART_DIR");
	fillPoolOption(req, options.startupFile, "!~PASSENGER_STARTUP_FILE");
	fillPoolOption(req, options.loadShellEnvvars, "!~PASSENGER_LOAD_SHELL_ENVVARS");
	fillPoolOption(req, options.debugger, "!~PASSENGER_DEBUGGER");
	fillPoolOption(req, options.raiseInternalError, "!~PASSENGER_RAISE_INTERNAL_ERROR");
	/******************/

	ServerKit::HeaderTable::Iterator it(secureHeaders);
	while (*it != NULL) {
		ServerKit::Header *header = it->header;
		if (!psg_lstr_cmp(&header->key, P_STATIC_STRING("!~PASSENGER_"), sizeof("!~PASSENGER_") - 1)) {
			LString *key = psg_lstr_make_contiguous(&header->key, req->pool);
			LString *val = psg_lstr_make_contiguous(&header->val, req->pool);
			options.environmentVariables.push_back(make_pair(
				StaticString(key->start->data, key->size),
				StaticString(val->start->data, val->size)
			));
		}
		it.next();
	}

	boost::shared_ptr<Options> optionsCopy = make_shared<Options>(options);
	optionsCopy->persist(options);
	optionsCopy->clearPerRequestFields();
	optionsCopy->detachFromUnionStationTransaction();
	poolOptionsCache.insert(options.getAppGroupName(), optionsCopy);
}

void
initializeUnionStation(Client *client, Request *req) {
	// TODO
}

void
setStickySessionId(Client *client, Request *req) {
	const LString *value = req->headers.lookup(PASSENGER_STICKY_SESSIONS);
	if (value != NULL && value->size > 0 && psg_lstr_first_byte(value) == 't') {
		// TODO: This is not entirely correct. Clients MAY send multiple Cookie
		// headers, although this is in practice extremely rare.
		// http://stackoverflow.com/questions/16305814/are-multiple-cookie-headers-allowed-in-an-http-request
		const LString *cookieHeader = req->headers.lookup(HTTP_COOKIE);
		const LString *cookieName = getStickySessionCookieName(req);
		vector< pair<StaticString, StaticString> > cookies;
		pair<StaticString, StaticString> cookie;

		req->stickySession = true;
		parseCookieHeader(req->pool, cookieHeader, cookies);
		foreach (cookie, cookies) {
			if (psg_lstr_cmp(cookieName, cookie.first)) {
				// This cookie matches the one we're looking for.
				req->options.stickySessionId = stringToUint(cookie.second);
				return;
			}
		}
	}
}

const LString *
getStickySessionCookieName(Request *req) {
	const LString *value = req->headers.lookup(PASSENGER_STICKY_SESSIONS_COOKIE_NAME);
	if (value == NULL || value->size == 0) {
		return psg_lstr_create(req->pool,
			P_STATIC_STRING(DEFAULT_STICKY_SESSIONS_COOKIE_NAME));
	} else {
		return value;
	}
}
