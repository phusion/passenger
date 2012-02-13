/*
 *  Phusion Passenger - http://www.modrails.com/
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
Client::onAppInputError(const EventedBufferedInputPtr &source, const char *message, int errnoCode) {
	Client *client = (Client *) source->userData;
	if (client != NULL) {
		client->requestHandler->onAppInputError(client->shared_from_this(), message, errnoCode);
	}
}


void
Client::onAppOutputWritable(ev::io &io, int revents) {
	assert(requestHandler != NULL);
	requestHandler->onAppOutputWritable(shared_from_this());
}


} // namespace Passenger

#ifdef STANDALONE
#include <MultiLibeio.cpp>
#include <iostream>

using namespace std;
using namespace Passenger;
using namespace Passenger::ApplicationPool2;

static RequestHandler *handler;
struct ev_loop *loop;

static void
sighup_cb(struct ev_loop *loop, ev_signal *w, int revents) {
	handler->inspect(cout);
}

static void
sigint_cb(struct ev_loop *loop, ev_signal *w, int revents) {
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
	printf("Client: %d\n", (int)sizeof(Client));
	setup_syscall_interruption_support();
	ignoreSigpipe();
	setLogLevel(3);
	MultiLibeio::init();
	loop = EV_DEFAULT;
	SafeLibevPtr libev = make_shared<SafeLibev>(loop);
	AgentOptions options;
	ServerInstanceDir serverInstanceDir(getpid());
	#ifdef __linux__
		const char *nogroup = "nogroup";
		const char *root = "/home/hongli/Projects/passenger";
	#else
		const char *nogroup = "nobody";
		const char *root = "/Users/hongli/Projects/passenger";
	#endif
	options.passengerRoot = root;
	SpawnerFactoryPtr spawnerFactory = make_shared<SpawnerFactory>(libev,
		ResourceLocator(root),
		serverInstanceDir.newGeneration(true, "nobody", nogroup, getpid(), getgid()));
	PoolPtr pool = make_shared<Pool>(libev.get(), spawnerFactory);
	FileDescriptor requestSocket = createTcpServer("127.0.0.1", 3000);
	setNonBlocking(requestSocket);
	handler = new RequestHandler(libev, requestSocket, pool, options);
	
	struct ev_signal sighupwatcher;
	ev_signal_init(&sighupwatcher, sighup_cb, SIGQUIT);
	ev_signal_start(loop, &sighupwatcher);

	struct ev_signal sigintwatcher;
	ev_signal_init(&sigintwatcher, sigint_cb, SIGINT);
	ev_signal_start(loop, &sigintwatcher);
	
	P_DEBUG("Started");
	ev_run(loop, 0);
	delete handler;
	return 0;
}
#endif
