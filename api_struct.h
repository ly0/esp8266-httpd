#include "http_request.h"
#define REQ_TYPE_GET 1
#define REQ_TYPE_POST 2
#define MAX_API_CONTENT 4096
#define MAX_PARAM 40 /* maximun params */

typedef const char* (*router_handler)(HTTPRequest *, void *);

typedef struct url_route
{
	const char *url;
	router_handler func;
} URLRouter, *pURLRouter;

typedef struct params
{
	const char *key;
	const char *value;
} Params;


