// This file is included inside the RequestHandler class.
// It implements the most important ServerKit::Server hooks.

protected:

virtual void
onClientAccepted(Client *client) {
	ParentClass::onClientAccepted(client);
	client->connectedAt = ev_now(getLoop());
}

virtual void
onRequestObjectCreated(Client *client, Request *req) {
	ParentClass::onRequestObjectCreated(client, req);
	req->appInput.setContext(getContext());
	req->appInput.setHooks(&req->hooks);
	req->appOutput.setContext(getContext());
	req->appOutput.setHooks(&req->hooks);
	//req.appOutput.errorCallback = ...;
	//req.appOutput.setFlushedCallback();

	//req.responseDechunker.onData   = onAppInputChunk;
	//req.responseDechunker.onEnd    = onAppInputChunkEnd;
	//req.responseDechunker.userData = this;
}
