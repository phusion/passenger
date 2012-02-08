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
Client::onClientInputData(weak_ptr<Client> wself, const StaticString &data) {
	shared_ptr<Client> self = wself.lock();
	if (self != NULL) {
		return self->requestHandler->onClientInputData(self, data);
	} else {
		return 0;
	}
}

void
Client::onClientInputError(weak_ptr<Client> wself, const char *message, int errnoCode) {
	shared_ptr<Client> self = wself.lock();
	if (self != NULL) {
		self->requestHandler->onClientInputError(self, message, errnoCode);
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
Client::onClientBodyBufferDrained(const FileBackedPipePtr &source) {
	Client *client = (Client *) source->userData;
	if (client != NULL) {
		client->requestHandler->onClientBodyBufferDrained(client->shared_from_this());
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
Client::onClientOutputPipeDrained(const FileBackedPipePtr &source) {
	Client *client = (Client *) source->userData;
	if (client != NULL) {
		client->requestHandler->onClientOutputPipeDrained(client->shared_from_this());
	}
}


void
Client::onClientOutputWritable(ev::io &io, int revents) {
	assert(requestHandler != NULL);
	requestHandler->onClientOutputWritable(shared_from_this());
}


size_t
Client::onAppInputData(weak_ptr<Client> wself, const StaticString &data) {
	shared_ptr<Client> self = wself.lock();
	if (self != NULL) {
		return self->requestHandler->onAppInputData(self, data);
	} else {
		return 0;
	}
}

void
Client::onAppInputError(weak_ptr<Client> wself, const char *message, int errnoCode) {
	shared_ptr<Client> self = wself.lock();
	if (self != NULL) {
		self->requestHandler->onAppInputError(self, message, errnoCode);
	}
}


void
Client::onAppOutputWritable(ev::io &io, int revents) {
	assert(requestHandler != NULL);
	requestHandler->onAppOutputWritable(shared_from_this());
}


} // namespace Passenger
