#include "esp_common.h"
#include "api_struct.h"

const char* ICACHE_FLASH_ATTR
page_404(HTTPRequest *req, void *args)
{
	char *api_buffer = (char *)malloc(MAX_API_CONTENT);
	strcpy(api_buffer, "404 Not Found");
	return api_buffer;
}