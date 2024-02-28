#ifndef INCLUDE_LLERRORS_H_
#define INCLUDE_LLERRORS_H_

#include <ServerKit/llhttp.h>

inline const char *llhttp_get_error_description(llhttp_errno_t error) {
	switch (error) {
	case HPE_OK:
		return "success";
	case HPE_INTERNAL:
		return "encountered unexpected internal state";
	case HPE_STRICT:
		return "strict mode assertion failed";
	case HPE_CR_EXPECTED:
		return "CR character expected";
	case HPE_LF_EXPECTED:
		return "LF character expected";
	case HPE_UNEXPECTED_CONTENT_LENGTH:
		return "unexpected content-length header";
	case HPE_UNEXPECTED_SPACE:
		return "unexpected space character";
	case HPE_CLOSED_CONNECTION:
		return "data received after completed connection: close message";
	case HPE_INVALID_METHOD:
		return "invalid HTTP method";
	case HPE_INVALID_URL:
		return "invalid URL";
	case HPE_INVALID_CONSTANT:
		return "invalid constant string";
	case HPE_INVALID_VERSION:
		return "invalid HTTP version";
	case HPE_INVALID_HEADER_TOKEN:
		return "invalid character in header";
	case HPE_INVALID_CONTENT_LENGTH:
		return "invalid character in content-length header";
	case HPE_INVALID_CHUNK_SIZE:
		return "invalid character in chunk size header";
	case HPE_INVALID_STATUS:
		return "invalid HTTP status code";
	case HPE_INVALID_EOF_STATE:
		return "stream ended at an unexpected time";
	case HPE_INVALID_TRANSFER_ENCODING:
		return "request has invalid transfer-encoding";
	case HPE_PAUSED:
		return "parser is paused";
	case HPE_PAUSED_UPGRADE:
		return "Pause on CONNECT/Upgrade";
	case HPE_PAUSED_H2_UPGRADE:
		return "Pause on Http2 CONNECT/Upgrade";
	case HPE_USER:
		return "User callback error";
	case HPE_CB_MESSAGE_BEGIN:
		return "the on_message_begin callback failed";
	case HPE_CB_HEADERS_COMPLETE:
		return "the on_headers_complete callback failed";
	case HPE_CB_MESSAGE_COMPLETE:
		return "the on_message_complete callback failed";
	case HPE_CB_URL_COMPLETE:
		return "the on_url_complete callback failed";
	case HPE_CB_STATUS_COMPLETE:
		return "the on_status_complete callback failed";
	case HPE_CB_METHOD_COMPLETE:
		return "the on_method_complete callback failed";
	case HPE_CB_VERSION_COMPLETE:
		return "the on_version_complete callback failed";
	case HPE_CB_HEADER_FIELD_COMPLETE:
		return "the on_header_field_complete callback failed";
	case HPE_CB_HEADER_VALUE_COMPLETE:
		return "the on_header_value_complete callback failed";
	case HPE_CB_CHUNK_EXTENSION_NAME_COMPLETE:
		return "the on_chunk_extension_name_complete callback failed";
	case HPE_CB_CHUNK_EXTENSION_VALUE_COMPLETE:
		return "the on_chunk_extension_value_complete callback failed";
	case HPE_CB_CHUNK_HEADER:
		return "the on_chunk_header callback failed";
	case HPE_CB_CHUNK_COMPLETE:
		return "the on_chunk_complete callback failed";
	case HPE_CB_RESET:
		return "the on_reset callback failed";
	default:
		return "unknown error";
	}
}
#endif
