#ifndef _HTTP_REQUEST_H
#define _HTTP_REQUEST_H
typedef struct http_request {
	char *uri;
	char *post_data;
	uint8_t is_post;
	char *params;
} HTTPRequest;
#endif
