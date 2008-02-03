#include "MessageChannel.h"
#include "Utils.cpp"
#include <vector>

using namespace std;
using namespace Passenger;

static bool
handleRequest(MessageChannel &reader, MessageChannel &writer) {
	vector<string> headers;

	P_TRACE("Reading request headers");
	if (!reader.read(headers)) {
		return true;
	}
	P_TRACE("Done reading request headers");
	
	string content;
	content.reserve(1024 * 7);
	content += "<b>Using C++ DummyRequestHandler</b><br>\n";
	unsigned int i;
	for (i = 0; i < headers.size(); i += 2) {
		content += "<tt>";
		content += headers[i];
		content += " = ";
		content += headers[i + 1];
		content += "</tt><br>\n";
	}
	
	string header;
	header.reserve(512);
	header += "Status: 200 OK\r\n"
		"Content-Type: text/html\r\n"
		"Content-Length: ";
	header += toString(content.size());
	header += "\r\n\r\n";
	P_TRACE("Sending response header");
	writer.writeScalar(header);
	P_TRACE("Sending response content");
	writer.writeScalar(content);
	P_TRACE("All done");
	writer.writeScalar("", 0);
	return false;
}

int
main() {
	MessageChannel reader(STDIN_FILENO);
	MessageChannel writer(STDOUT_FILENO);
	bool done = false;
	initDebugging();
	while (!done) {
		done = handleRequest(reader, writer);
	}
	return 0;
}
