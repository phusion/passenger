/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2011, 2012 Phusion
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

#include <agents/HelperAgent/RequestHandler.h>

namespace Passenger {


struct ev_loop *
Client::getLoop() const {
	return requestHandler->libev->getLoop();
}

const SafeLibevPtr &
Client::getSafeLibev() const {
	return requestHandler->libev;
}

unsigned int
Client::getConnectPasswordTimeout(const RequestHandler *handler) const {
	return handler->connectPasswordTimeout;
}

size_t
Client::onClientInputData(const EventedBufferedInputPtr &source, const StaticString &data) {
	Client *client = (Client *) source->userData;
	if (client != NULL) {
		return client->requestHandler->onClientInputData(client->shared_from_this(), data);
	} else {
		return 0;
	}
}

void
Client::onClientInputError(const EventedBufferedInputPtr &source, const char *message, int errnoCode) {
	Client *client = (Client *) source->userData;
	if (client != NULL) {
		client->requestHandler->onClientInputError(client->shared_from_this(), message, errnoCode);
	}
}


void
Client::onClientBodyBufferData(const FileBackedPipePtr &source,
	const char *data, size_t size,
	const FileBackedPipe::ConsumeCallback &callback)
{
	Client *client = (Client *) source->userData;
	if (client != NULL) {
		client->requestHandler->onClientBodyBufferData(client->shared_from_this(),
			data, size, callback);
	}
}

void
Client::onClientBodyBufferEnd(const FileBackedPipePtr &source) {
	Client *client = (Client *) source->userData;
	if (client != NULL) {
		client->requestHandler->onClientBodyBufferEnd(client->shared_from_this());
	}
}

void
Client::onClientBodyBufferError(const FileBackedPipePtr &source, int errorCode) {
	Client *client = (Client *) source->userData;
	if (client != NULL) {
		client->requestHandler->onClientBodyBufferError(client->shared_from_this(), errorCode);
	}
}

void
Client::onClientBodyBufferCommit(const FileBackedPipePtr &source) {
	Client *client = (Client *) source->userData;
	if (client != NULL) {
		client->requestHandler->onClientBodyBufferCommit(client->shared_from_this());
	}
}


void
Client::onClientOutputPipeData(const FileBackedPipePtr &source,
	const char *data, size_t size,
	const FileBackedPipe::ConsumeCallback &callback)
{
	Client *client = (Client *) source->userData;
	if (client != NULL) {
		client->requestHandler->onClientOutputPipeData(client->shared_from_this(),
			data, size, callback);
	}
}

void
Client::onClientOutputPipeEnd(const FileBackedPipePtr &source) {
	Client *client = (Client *) source->userData;
	if (client != NULL) {
		client->requestHandler->onClientOutputPipeEnd(client->shared_from_this());
	}
}

void
Client::onClientOutputPipeError(const FileBackedPipePtr &source, int errorCode) {
	Client *client = (Client *) source->userData;
	if (client != NULL) {
		client->requestHandler->onClientOutputPipeError(client->shared_from_this(), errorCode);
	}
}

void
Client::onClientOutputPipeCommit(const FileBackedPipePtr &source) {
	Client *client = (Client *) source->userData;
	if (client != NULL) {
		client->requestHandler->onClientOutputPipeCommit(client->shared_from_this());
	}
}


void
Client::onClientOutputWritable(ev::io &io, int revents) {
	assert(requestHandler != NULL);
	requestHandler->onClientOutputWritable(shared_from_this());
}


size_t
Client::onAppInputData(const EventedBufferedInputPtr &source, const StaticString &data) {
	Client *client = (Client *) source->userData;
	if (client != NULL) {
		return client->requestHandler->onAppInputData(client->shared_from_this(), data);
	} else {
		return 0;
	}
}

void
Client::onAppInputChunk(const char *data, size_t size, void *userData) {
	Client *client = (Client *) userData;
	if (client != NULL && client->connected()) {
		client->requestHandler->onAppInputChunk(client->shared_from_this(), StaticString(data, size));
	}
}

void
Client::onAppInputChunkEnd(void *userData) {
	Client *client = (Client *) userData;
	assert(client != NULL);
	// onAppInputChunk() could have triggered an error which caused a disconnect.
	if (client->connected()) {
		client->requestHandler->onAppInputChunkEnd(client->shared_from_this());
	}
}

void
Client::onAppInputError(const EventedBufferedInputPtr &source, const char *message, int errnoCode) {
	Client *client = (Client *) source->userData;
	// onAppInputChunk() could have triggered an error which caused a disconnect.
	if (client != NULL && client->connected()) {
		client->requestHandler->onAppInputError(client->shared_from_this(), message, errnoCode);
	}
}


void
Client::onAppOutputWritable(ev::io &io, int revents) {
	assert(requestHandler != NULL);
	requestHandler->onAppOutputWritable(shared_from_this());
}


void
Client::onTimeout(ev::timer &timer, int revents) {
	assert(requestHandler != NULL);
	requestHandler->onTimeout(shared_from_this());
}


Client *
RequestHandler::getClientPointer(const ClientPtr &client) {
	return client.get();
}


} // namespace Passenger

#ifdef STANDALONE
#include <MultiLibeio.cpp>
#include <iostream>
#include <agents/Base.h>
#include <oxt/initialize.hpp>

using namespace std;
using namespace Passenger;
using namespace Passenger::ApplicationPool2;

static SafeLibevPtr libev;
static RequestHandler *handler;
static struct ev_loop *loop;
static PoolPtr pool;
static struct ev_signal sigquitwatcher, sigintwatcher;

static void
sigquit_cb(struct ev_loop *loop, ev_signal *w, int revents) {
	handler->inspect(cout);
	cout.flush();
}

static void
sigint_cb(struct ev_loop *loop, ev_signal *w, int revents) {
	P_WARN("Exiting loop");
	delete handler;
	handler = NULL;
	pool->destroy();
	pool.reset();
	ev_signal_stop(loop, &sigquitwatcher);
	ev_signal_stop(loop, &sigintwatcher);
	ev_break(loop, EVBREAK_ONE);
}

static void
ignoreSigpipe() {
	struct sigaction action;
	action.sa_handler = SIG_IGN;
	action.sa_flags   = 0;
	sigemptyset(&action.sa_mask);
	sigaction(SIGPIPE, &action, NULL);
}

int
main() {
	oxt::initialize();
	setup_syscall_interruption_support();
	ignoreSigpipe();
	//installAbortHandler();
	setLogLevel(3);
	MultiLibeio::init();
	loop = EV_DEFAULT;
	libev = boost::make_shared<SafeLibev>(loop);
	AgentOptions options;
	ServerInstanceDir serverInstanceDir(getpid());
	char root[PATH_MAX];
	getcwd(root, sizeof(root));
	#ifdef __linux__
		const char *nogroup = "nogroup";
	#else
		const char *nogroup = "nobody";
	#endif
	options.passengerRoot = root;
	options.loggingAgentAddress = "unix:/tmp/agent";
	options.loggingAgentPassword = "1234";

	SpawnerFactoryPtr spawnerFactory = boost::make_shared<SpawnerFactory>(libev,
		ResourceLocator(root),
		serverInstanceDir.newGeneration(true, "nobody", nogroup, getpid(), getgid()));
	UnionStation::LoggerFactoryPtr loggerFactory = boost::make_shared<UnionStation::LoggerFactory>(options.loggingAgentAddress,
		"logging", options.loggingAgentPassword);
	pool = boost::make_shared<Pool>(libev.get(), spawnerFactory, loggerFactory);
	FileDescriptor requestSocket(createTcpServer("127.0.0.1", 3000));
	setNonBlocking(requestSocket);
	handler = new RequestHandler(libev, requestSocket, pool, options);

	ev_signal_init(&sigquitwatcher, sigquit_cb, SIGQUIT);
	ev_signal_start(loop, &sigquitwatcher);

	ev_signal_init(&sigintwatcher, sigint_cb, SIGINT);
	ev_signal_start(loop, &sigintwatcher);

	P_DEBUG("Started");
	ev_run(loop, 0);

	MultiLibeio::shutdown();
	return 0;
}
#endif
