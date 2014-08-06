#ifndef _PASSENGER_SERVER_KIT_REQUEST_RESPONSE_SERVER_H_
#define _PASSENGER_SERVER_KIT_REQUEST_RESPONSE_SERVER_H_

#include <ServerKit/Context.h>
#include <ServerKit/Client.h>
#include <ServerKit/FdDataSource.h>

namespace Passenger {
namespace ServerKit {


class RequestResponseClient: public Client {
public:
	Hooks hooks;
	FdDataSource requestDataSource;

	RequestResponseClient(void *server)
		: Client(server)
		{ }

	void associate(int fd) {
		Client::associate(fd);
		requestDataSource.reset(fd);
	}

	void reset() {
		Client::reset();
		requestDataSource.reset();
	}
};


} // namespace ServerKit
} // namespace Passenger

#endif /* _PASSENGER_SERVER_KIT_REQUEST_RESPONSE_SERVER_H_ */
