#include <TestSupport.h>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <boost/thread.hpp>
#include <oxt/system_calls.hpp>
#include <vector>
#include <BackgroundEventLoop.h>
#include <ServerKit/Server.h>
#include <ServerKit/ClientRef.h>
#include <Logging.h>
#include <FileDescriptor.h>
#include <Utils/IOUtils.h>

using namespace Passenger;
using namespace Passenger::ServerKit;
using namespace Passenger::MemoryKit;
using namespace std;
using namespace oxt;

namespace tut {
	struct ServerKit_ServerTest {
		typedef ClientRef<Server<Client>, Client> ClientRefType;

		BackgroundEventLoop bg;
		ServerKit::Context context;
		boost::shared_ptr< Server<Client> > server;
		int serverSocket1, serverSocket2;

		ServerKit_ServerTest()
			: bg(false, true),
			  context(bg.safe)
		{
			initializeLibeio();
			setLogLevel(LVL_CRIT);
			serverSocket1 = createUnixServer("tmp.server1");
			serverSocket2 = createUnixServer("tmp.server2");
			server = boost::make_shared< Server<Client> >(&context);
			server->listen(serverSocket1);
		}

		~ServerKit_ServerTest() {
			if (!bg.isStarted()) {
				bg.start();
			}
			bg.safe->runSync(boost::bind(&Server<Client>::shutdown, server.get(), true));
			safelyClose(serverSocket1);
			safelyClose(serverSocket2);
			unlink("tmp.server1");
			unlink("tmp.server2");
			setLogLevel(DEFAULT_LOG_LEVEL);
			bg.stop();
			shutdownLibeio();
		}

		void startServer() {
			bg.start();
		}

		FileDescriptor connectToServer1() {
			return FileDescriptor(connectToUnixServer("tmp.server1"));
		}

		FileDescriptor connectToServer2() {
			return FileDescriptor(connectToUnixServer("tmp.server2"));
		}

		unsigned int getActiveClientCount() {
			int result;
			bg.safe->runSync(boost::bind(&ServerKit_ServerTest::_getActiveClientCount,
				this, &result));
			return result;
		}

		void _getActiveClientCount(int *result) {
			*result = server->activeClientCount;
		}

		unsigned int getDisconnectedClientCount() {
			int result;
			bg.safe->runSync(boost::bind(&ServerKit_ServerTest::_getDisconnectedClientCount,
				this, &result));
			return result;
		}

		void _getDisconnectedClientCount(int *result) {
			*result = server->disconnectedClientCount;
		}

		unsigned int getFreeClientCount() {
			int result;
			bg.safe->runSync(boost::bind(&ServerKit_ServerTest::_getFreeClientCount,
				this, &result));
			return result;
		}

		void _getFreeClientCount(int *result) {
			*result = server->freeClientCount;
		}

		vector<ClientRefType> getActiveClients() {
			vector<ClientRefType> result;
			bg.safe->runSync(boost::bind(&ServerKit_ServerTest::_getActiveClients,
				this, &result));
			return result;
		}

		void _getActiveClients(vector<ClientRefType> *result) {
			*result = server->getActiveClients();
		}

		bool clientIsConnected(Client *client) {
			bool result;
			bg.safe->runSync(boost::bind(&ServerKit_ServerTest::_clientIsConnected,
				this, client, &result));
			return result;
		}

		void _clientIsConnected(Client *client, bool *result) {
			*result = client->connected();
		}
	};

	DEFINE_TEST_GROUP(ServerKit_ServerTest);

	#define USE_CUSTOM_SERVER_CLASS(Klass) \
		server->shutdown(); \
		server = boost::make_shared< Klass >(&context); \
		server->listen(serverSocket1)


	/***** Initial state *****/

	TEST_METHOD(1) {
		set_test_name("It has no clients at startup");
		ensure_equals(server->activeClientCount, 0u);
		ensure_equals(server->disconnectedClientCount, 0u);
		ensure_equals(server->freeClientCount, 0u);
	}


	/***** Client object management *****/

	TEST_METHOD(5) {
		set_test_name("Accepting a new client works");

		startServer();
		FileDescriptor fd = connectToServer1();
		EVENTUALLY(5,
			result = getActiveClientCount() == 1u;
		);
	}

	TEST_METHOD(6) {
		set_test_name("When a client is accepted, and the freelist is non-empty, "
			"the object is checked out from the freelist");

		server->minSpareClients = 1;
		server->createSpareClients();
		startServer();

		ensure_equals(getActiveClientCount(), 0u);
		ensure_equals(getDisconnectedClientCount(), 0u);
		ensure_equals(getFreeClientCount(), 1u);

		FileDescriptor fd = connectToServer1();
		EVENTUALLY(5,
			result = getActiveClientCount() == 1u;
		);
		ensure_equals(getDisconnectedClientCount(), 0u);
		ensure_equals(getFreeClientCount(), 0u);
	}

	TEST_METHOD(7) {
		set_test_name("When a client is accepted, and the freelist is empty, "
			"the object is allocated");

		startServer();
		FileDescriptor fd = connectToServer1();
		EVENTUALLY(5,
			result = getActiveClientCount() == 1u;
		);
		ensure_equals(getDisconnectedClientCount(), 0u);
		ensure_equals(getFreeClientCount(), 0u);
	}

	TEST_METHOD(8) {
		set_test_name("Once a client has been disconnected, and the freelist has not "
			"yet reached the limit, the client object is put on the freelist");

		server->clientFreelistLimit = 10;
		startServer();

		FileDescriptor fd = connectToServer1();
		EVENTUALLY(5,
			result = getActiveClientCount() == 1u;
		);
		fd.close();
		EVENTUALLY(5,
			result = getActiveClientCount() == 0u;
		);
		ensure_equals(getDisconnectedClientCount(), 0u);
		ensure_equals(getFreeClientCount(), 1u);
	}

	TEST_METHOD(9) {
		set_test_name("Once a client has been disconnected, and the freelist has "
			"reached the limit, the client object is destroyed");

		server->clientFreelistLimit = 0;
		startServer();

		FileDescriptor fd = connectToServer1();
		EVENTUALLY(5,
			result = getActiveClientCount() == 1u;
		);
		fd.close();
		EVENTUALLY(5,
			result = getActiveClientCount() == 0u;
		);
		ensure_equals(getDisconnectedClientCount(), 0u);
		ensure_equals(getFreeClientCount(), 0u);
	}

	TEST_METHOD(10) {
		set_test_name("Once a client has been disconnected, the freelist has not yet reached "
			"the limit, and there are still references to the client, then the client is first "
			"put in the disconnecting list, then in the freelist when the last references disappear");

		vector<ClientRefType> clients;
		server->clientFreelistLimit = 10;
		startServer();

		FileDescriptor fd = connectToServer1();
		EVENTUALLY(5,
			result = getActiveClientCount() == 1u;
		);

		clients = getActiveClients();
		fd.close();
		EVENTUALLY(5,
			result = getActiveClientCount() == 0u;
		);
		ensure_equals(getDisconnectedClientCount(), 1u);
		ensure_equals(getFreeClientCount(), 0u);

		clients.clear();
		EVENTUALLY(5,
			result = getDisconnectedClientCount() == 0u;
		);
		ensure_equals(getActiveClientCount(), 0u);
		ensure_equals(getFreeClientCount(), 1u);
	}

	TEST_METHOD(11) {
		set_test_name("Once a client has been disconnected, the freelist has reached the limit, "
			"and there are still references to the client, then the client is first put in the "
			"disconnecting list, then destroyed when the last references disappear");

		vector<ClientRefType> clients;
		server->clientFreelistLimit = 0;
		startServer();

		FileDescriptor fd = connectToServer1();
		EVENTUALLY(5,
			result = getActiveClientCount() == 1u;
		);

		clients = getActiveClients();
		fd.close();
		EVENTUALLY(5,
			result = getActiveClientCount() == 0u;
		);
		ensure_equals(getDisconnectedClientCount(), 1u);
		ensure_equals(getFreeClientCount(), 0u);

		clients.clear();
		EVENTUALLY(5,
			result = getDisconnectedClientCount() == 0u;
		);
		ensure_equals(getActiveClientCount(), 0u);
		ensure_equals(getFreeClientCount(), 0u);
	}


	/****** Multiple listen endpoints *****/

	TEST_METHOD(20) {
		set_test_name("It can listen on multiple endpoints");

		server->listen(serverSocket2);
		startServer();

		FileDescriptor fd1 = connectToServer1();
		FileDescriptor fd2 = connectToServer2();
		EVENTUALLY(5,
			result = getActiveClientCount() == 2u;
		);
	}


	/****** Input and output *****/

	class Test25Server: public Server<Client> {
	protected:
		virtual Channel::Result onClientDataReceived(Client *client,
			const MemoryKit::mbuf &buffer, int errcode)
		{
			if (errcode != 0 || buffer.empty()) {
				disconnect(&client);
			} else {
				boost::lock_guard<boost::mutex> l(syncher);
				data.append(buffer.start, buffer.size());
			}
			return Channel::Result(buffer.size(), false);
		}

	public:
		boost::mutex syncher;
		string data;

		Test25Server(Context *ctx)
			: Server<Client>(ctx)
			{ }
	};

	TEST_METHOD(25) {
		set_test_name("Input is made available through client->input");

		USE_CUSTOM_SERVER_CLASS(Test25Server);
		startServer();

		FileDescriptor fd = connectToServer1();
		writeExact(fd, "hello", 5);

		EVENTUALLY(5,
			Test25Server *s = (Test25Server *) server.get();
			boost::lock_guard<boost::mutex> l(s->syncher);
			result = s->data == "hello";
		);
	}

	class Test26Server: public Server<Client> {
	protected:
		virtual Channel::Result onClientDataReceived(Client *client,
			const MemoryKit::mbuf &buffer, int errcode)
		{
			if (errcode != 0 || buffer.empty()) {
				disconnect(&client);
			} else {
				client->output.feed(buffer);
			}
			return Channel::Result(buffer.size(), false);
		}

	public:
		Test26Server(Context *ctx)
			: Server<Client>(ctx)
			{ }
	};

	TEST_METHOD(26) {
		set_test_name("Output is made available through client->output");

		USE_CUSTOM_SERVER_CLASS(Test26Server);
		startServer();

		FileDescriptor fd = connectToServer1();
		writeExact(fd, "hello", 5);
		syscalls::shutdown(fd, SHUT_WR);
		ensure_equals(readAll(fd), "hello");
	}

	TEST_METHOD(27) {
		set_test_name("The file descriptor can be obtained through client->getFd()");

		startServer();

		FileDescriptor fd = connectToServer1();
		EVENTUALLY(5,
			result = getActiveClientCount() == 1u;
		);

		ClientRefType client = getActiveClients()[0];
		writeExact(client.get()->getFd(), "hello", 5);

		char buf[5];
		readExact(fd, buf, 5);
		ensure_equals(StaticString(buf, 5), StaticString("hello"));
	}

	TEST_METHOD(28) {
		set_test_name("When a client is disconnected, client->connected() becomes false");

		startServer();

		FileDescriptor fd = connectToServer1();
		EVENTUALLY(5,
			result = getActiveClientCount() == 1u;
		);

		ClientRefType client = getActiveClients()[0];
		ensure(clientIsConnected(client.get()));
		fd.close();
		EVENTUALLY(5,
			result = !clientIsConnected(client.get());
		);
	}
}
