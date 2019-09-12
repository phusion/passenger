/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2014-2017 Phusion Holding B.V.
 *
 *  "Passenger", "Phusion Passenger" and "Union Station" are registered
 *  trademarks of Phusion Holding B.V.
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
#include <Core/Controller.h>

/*************************************************************************
 *
 * Miscellaneous functions for Core::Controller
 *
 *************************************************************************/

namespace Passenger {
namespace Core {

using namespace std;
using namespace boost;


/****************************
 *
 * Public methods
 *
 ****************************/


void
Controller::disconnectLongRunningConnections(const StaticString &gupid) {
	vector<Client *> clients;
	vector<Client *>::iterator v_it, v_end;
	Client *client;

	// We collect all clients in a vector so that we don't have to worry about
	// `activeClients` being mutated while we work.
	TAILQ_FOREACH (client, &activeClients, nextClient.activeOrDisconnectedClient) {
		P_ASSERT_EQ(client->getConnState(), Client::ACTIVE);
		if (client->currentRequest != NULL) {
			Request *req = client->currentRequest;
			if (req->httpState >= Request::COMPLETE
			 && req->upgraded()
			 && req->options.abortWebsocketsOnProcessShutdown
			 && req->session != NULL
			 && req->session->getGupid() == gupid)
			{
				if (LoggingKit::getLevel() >= LoggingKit::INFO) {
					char clientName[32];
					unsigned int size;
					const LString *host;
					StaticString hostStr;

					size = getClientName(client, clientName, sizeof(clientName));
					if (req->host != NULL && req->host->size > 0) {
						host = psg_lstr_make_contiguous(req->host, req->pool);
						hostStr = StaticString(host->start->data, host->size);
					}
					P_INFO("[" << getServerName() << "] Disconnecting client " <<
						StaticString(clientName, size) << ": " <<
						hostStr << StaticString(req->path.start->data, req->path.size));
				}
				refClient(client, __FILE__, __LINE__);
				clients.push_back(client);
			}
		}
	}

	// Disconnect each eligible client.
	v_end = clients.end();
	for (v_it = clients.begin(); v_it != v_end; v_it++) {
		client = *v_it;
		Client *c = client;
		disconnect(&client);
		unrefClient(c, __FILE__, __LINE__);
	}
}


} // namespace Core
} // namespace Passenger
