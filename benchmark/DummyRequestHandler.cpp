#include "MessageChannel.h"
#include "Utils.cpp"
#include <vector>
#include <unistd.h>
#include <errno.h>

using namespace std;
using namespace Passenger;

typedef vector< pair<string, string> > HeaderSet;

static void
readHeaders(int reader, HeaderSet &headers) {
	string buffer;
	char buffer2[1024 * 32];
	buffer.reserve(1024 * 32);
	while (true) {
		ssize_t ret = read(reader, buffer2, sizeof(buffer2));
		if (ret == 0) {
			break;
		} else {
			buffer.append(buffer2, ret);
		}
	}
	
	string::size_type start = 0;
	string::size_type pos;
	while (true) {
		pos = buffer.find('\0', start);
		if (pos != string::npos) {
			string name(buffer.substr(start, pos - start));
			start = pos + 1;
			pos = buffer.find('\0', start);
			string value(buffer.substr(start, pos - start));
			start = pos + 1;
			headers.push_back(make_pair(name, value));
		} else {
			break;
		}
	}
}

static void
processRequest(int reader, int writer) {
	HeaderSet headers;
	readHeaders(reader, headers);
	close(reader);
	
	string content;
	content.reserve(1024 * 7);
	content += "<b>Using C++ DummyRequestHandler</b><br>\n";
	for (HeaderSet::const_iterator it(headers.begin()); it != headers.end(); it++) {
		content.append("<tt>");
		content.append(it->first);
		content.append(" = ");
		content.append(it->second);
		content.append("</tt><br>\n");
	}
	
	string header;
	header.reserve(512);
	header.append("Status: 200 OK\r\n"
		"Content-Type: text/html\r\n"
		"Content-Length: ");
	header.append(toString(content.size()));
	header.append("\r\n\r\n");
	
	MessageChannel channel(writer);
	channel.writeRaw(header);
	channel.writeRaw(content);
	channel.close();
}

static bool
acceptNextRequest(int fd) {
	char c;
	if (read(fd, &c, 1) == 0) {
		return true;
	}
	
	MessageChannel listener(fd);
	int fd1[2], fd2[2];
	pipe(fd1);
	pipe(fd2);
	listener.writeFileDescriptor(fd1[0]);
	listener.writeFileDescriptor(fd2[1]);
	close(fd1[0]);
	close(fd2[1]);
	processRequest(fd2[0], fd1[1]);
	return false;
}

int
main() {
	bool done = false;
	initDebugging();
	while (!done) {
		done = acceptNextRequest(STDIN_FILENO);
	}
	return 0;
}
