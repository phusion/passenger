#ifndef _PASSENGER_SERVER_KIT_REQUEST_RESPONSE_SERVER_H_
#define _PASSENGER_SERVER_KIT_REQUEST_RESPONSE_SERVER_H_

#include <ServerKit/Server.h>

namespace Passenger {
namespace ServerKit {


template<typename DerivedServer, typename Client>
class RequestResponseServer: public Server<DerivedServer, Client> {
private:
	static int _onClientDataReceived(FdDataSource *source,
		const MemoryKit::mbuf &buffer, int errcode)
	{
		Client *client = static_cast<Client *>(source->getHooks()->userData);
		RequestResponseServer *server = static_cast<RequestResponseServer *>(client->server);
		return server->onClientDataReceived(client, source, buffer, errcode);
	}

	static void hook_beforeEvent(Hooks *hooks, void *source) {
		Client *client = static_cast<Client *>(hooks->userData);
		RequestResponseServer *server = static_cast<RequestResponseServer *>(client->server);
		server->_refClient(client);
	}

	static void hook_afterEvent(Hooks *hooks, void *source) {
		Client *client = static_cast<Client *>(hooks->userData);
		RequestResponseServer *server = static_cast<RequestResponseServer *>(client->server);
		server->_unrefClient(client);
	}

	int onClientDataReceived(Client *client, FdDataSource *source,
		const MemoryKit::mbuf &buffer, int errcode)
	{
		static const char data[] =
			"RequestResponse/1.1 200 OK\r\n"
			"Status: 200 OK\r\n"
			"Content-Type: text/plain\r\n"
			"Content-Length: 3\r\n"
			"Connection: close\r\n"
			"\r\n"
			"ok\n";
		write(client->fd, data, sizeof(data) - 1);
		disconnect(&client);
		return buffer.size();
	}

protected:
	virtual void onClientCreated(Client *client) {
		Server<DerivedServer, Client>::onClientCreated(client);
		client->requestDataSource.setHooks(&client->hooks);
		client->requestDataSource.callback = _onClientDataReceived;
		client->hooks.beforeEvent = hook_beforeEvent;
		client->hooks.afterEvent  = hook_afterEvent;
		client->hooks.userData    = this;
	}

public:
	RequestResponseServer(Context *context)
		: Server<DerivedServer, Client>(context)
		{ }
};


} // namespace ServerKit
} // namespace Passenger

#endif /* _PASSENGER_SERVER_KIT_REQUEST_RESPONSE_SERVER_H_ */
