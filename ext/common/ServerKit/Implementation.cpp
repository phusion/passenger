#include <DataStructures/HashedStaticString.h>

namespace Passenger {
namespace ServerKit {


extern const HashedStaticString TRANSFER_ENCODING;
extern const char DEFAULT_INTERNAL_SERVER_ERROR_RESPONSE[];
extern const unsigned int DEFAULT_INTERNAL_SERVER_ERROR_RESPONSE_SIZE;

const HashedStaticString TRANSFER_ENCODING("transfer-encoding");
const char DEFAULT_INTERNAL_SERVER_ERROR_RESPONSE[] =
	"Status: 500 Internal Server Error\r\n"
	"Content-Length: 22\r\n"
	"Content-Type: text/plain\r\n"
	"Connection: close\r\n"
	"\r\n"
	"Internal server error\n";
const unsigned int DEFAULT_INTERNAL_SERVER_ERROR_RESPONSE_SIZE =
	sizeof(DEFAULT_INTERNAL_SERVER_ERROR_RESPONSE) - 1;

// The following functions are defined in this compilation unit so that they're
// compiled with optimizations on by default.

void
forceLowerCase(unsigned char *data, size_t len) {
	const unsigned char *end = data + len;
	while (data < end) {
		unsigned char c = *data;
		*data = ((c >= 'A' && c <= 'Z') ? (c | 0x20) : c);
		data++;
	}
}

} // namespace ServerKit
} // namespace
