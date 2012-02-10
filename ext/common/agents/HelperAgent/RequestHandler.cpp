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
