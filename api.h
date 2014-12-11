/* First define our url router data structure*/

#ifndef _API_H
#define _API_H
#include "api_struct.h"
#define URLS_ROUTE_LEN (sizeof(router_urls) / sizeof(URLRouter))

extern const char* page_index(HTTPRequest *, void*);
extern const char* page_ssid(HTTPRequest *, void*);
extern const char* page_404(HTTPRequest *, void*);

URLRouter page_err_404 = {
	"/404.html", page_404
};

URLRouter router_urls[] = {
	{"/", page_index},
	{"/ssid", page_ssid}
};

#endif