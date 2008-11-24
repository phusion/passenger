// g++ -Wall -I.. -I../apache2 Server.cpp -o server ../libboost_oxt.a ../apache2/Utils.o ../apache2/Logging.o -lpthread
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <cstring>
#include <unistd.h>

#include <set>

#include <boost/thread.hpp>
#include <boost/shared_ptr.hpp>
#include "oxt/thread.hpp"

#include "ScgiRequestParser.h"
#include "HttpStatusExtractor.h"

#include "StandardApplicationPool.h"
#include "Application.h"
#include "PoolOptions.h"
#include "Exceptions.h"
#include "Utils.h"

using namespace boost;
using namespace oxt;
using namespace Passenger;

class Client {
private:
	static const int CLIENT_THREAD_STACK_SIZE = 1024 * 128;
	
	StandardApplicationPoolPtr pool;
	int serverSocket;
	oxt::thread *thr;
	
	void processConnection() {
		struct sockaddr_un addr;
		socklen_t addrlen = sizeof(addr);
		int fd;
		ScgiRequestParser parser;
		
		fd = accept(serverSocket, (struct sockaddr *) &addr, &addrlen);
		//printf("------ New client!\n");
		char buf[1024 * 16];
		ssize_t size;
		unsigned int accepted;
		do {
			size = recv(fd, buf, sizeof(buf), 0);
			accepted = parser.feed(buf, size);
			//printf("\nsize = %d, accepted = %u, state = %d\n", size, accepted, parser.getState());
		} while (parser.acceptingInput());
		/* fwrite(parser.getHeaderData().c_str(), 1, parser.getHeaderData().size(), stdout);
		printf("\n");
		fflush(stdout);
		cout << parser.getHeader("REQUEST_METHOD") << " -> " << parser.getHeader("REQUEST_URI") << endl; */
		
		PoolOptions options("/var/www/projects/typo-5.0.3");
		//options.environment = "development";
		Application::SessionPtr session(pool->get(options));
		session->sendHeaders(parser.getHeaderData().c_str(), parser.getHeaderData().size());
		session->shutdownWriter();
		int stream = session->getStream();
		MessageChannel output(fd);
		bool eof = false;
		HttpStatusExtractor ex;
		
		while (!eof) {
			size = read(stream, buf, sizeof(buf));
			if (size <= 0) {
				eof = true;
			} else if (ex.feed(buf, size)) {
				string statusLine("HTTP/1.1 ");
				statusLine.append(ex.getStatusLine());
				output.writeRaw(statusLine.c_str(), statusLine.size());
				output.writeRaw(ex.getBuffer().c_str(), ex.getBuffer().size());
				//cout << statusLine << ex.getBuffer() << endl;
				break;
			}
		}
		while (!eof) {
			size = read(stream, buf, sizeof(buf));
			if (size <= 0) {
				eof = true;
			} else {
				output.writeRaw(buf, size);
				//cout << string(buf, size) << endl;
			}
		}
		session.reset();
		
		//printf("DONE\n");
		close(fd);
	}
	
	void threadMain() {
		while (true) {
			try {
				processConnection();
			} catch (const exception &e) {
				fprintf(stderr, "*** EXCEPTION: %s\n", e.what());
			}
		}
	}
	
public:
	Client(StandardApplicationPoolPtr &pool, int serverSocket) {
		this->pool = pool;
		this->serverSocket = serverSocket;
		thr = new oxt::thread(
			bind(&Client::threadMain, this),
			"Thread", CLIENT_THREAD_STACK_SIZE
		);
	}
};

typedef shared_ptr<Client> ClientPtr;

class Server {
private:
	int fd;
	set<ClientPtr> clients;
	StandardApplicationPoolPtr pool;

public:
	Server() {
		pool = ptr(new StandardApplicationPool(
			"/home/hongli/Projects/commercial_passenger/bin/passenger-spawn-server",
			"",
			"/opt/r8ee/bin/ruby"
		));
		pool->setMax(6);
		
		fd = socket(PF_UNIX, SOCK_STREAM, 0);
		
		struct sockaddr_un addr;
		addr.sun_family = AF_UNIX;
		strncpy(addr.sun_path, "/tmp/passenger_scgi.sock", sizeof(addr.sun_path));
		addr.sun_path[sizeof(addr.sun_path) - 1] = '\0';
		unlink("/tmp/passenger_scgi.sock");
		bind(fd, (const struct sockaddr *) &addr, sizeof(addr));
		
		listen(fd, 50);
	}
	
	void start() {
		for (unsigned int i = 0; i < 12; i++) {
			ClientPtr client(new Client(pool, fd));
			clients.insert(client);
		}
		while (true) {
			sleep(1);
		}
	}
};

int
main() {
	Server().start();
	return 0;
}

