#include "esp_common.h"
#include "api_struct.h"

const char* ICACHE_FLASH_ATTR
page_ssid(HTTPRequest *req, void *args)
{
	char *api_buffer = (char *)malloc(MAX_API_CONTENT);
	
	if(0 == req->is_post)
	{
		struct softap_config apconfig;
		const char template[] = "{" \
								"\"SSID\": \"%s\"," \
								"\"PASSWORD\": \"%s\"," \
								"\"CHANNEL\": %d," \
								"\"AUTHMODE\": %d," \
								"\"SSID_HIDDEN\": %d," \
								"\"MAX_CONNECTION\": %d" \
								"}";
		/* GET */
		wifi_softap_get_config(&apconfig);
		/* GET */
		const char method[] = "GET";
		char *params = req->params;

		sprintf(api_buffer, template, (char *)apconfig.ssid, \
									  (char *)apconfig.password, \
									  apconfig.channel, \
									  apconfig.authmode, \
									  apconfig.ssid_hidden, \
									  apconfig.max_connection);
	} else {
		/* POST for change softap config */
		const char method[] = "POST";
		char *params = req->params;
		printf("params: %s \n", params);
		uint16_t para_amount;
		uint16_t iter;
		Params para[MAX_PARAM];
		para_amount = extract_params(params, para);
		printf("[*] parsed %d parameters \n", para_amount);
		/* rtn data*/
		sprintf(api_buffer, "POST DATA TEST:\n");
		/* parameters */
		strcat(api_buffer, "parameters:\n");
		printf("\n\n%s\n\n", api_buffer);
		for(iter = 0; iter < para_amount; iter++)
		{
			strcat(api_buffer, "key:");
			strcat(api_buffer, para[iter].key);
			strcat(api_buffer, "\tvalue:");
			strcat(api_buffer, para[iter].value);
		}
		printf("\n\n%s\n\n", api_buffer);
		strcat(api_buffer, "\n");
		strcat(api_buffer, "post data:\n");
		para_amount = extract_params(req->post_data, para);
		printf("[*] parsed %d data_post \n", para_amount);
		for(iter = 0; iter < para_amount; iter++)
		{
			strcat(api_buffer, "key:");
			strcat(api_buffer, para[iter].key);
			strcat(api_buffer, "\tvalue:");
			strcat(api_buffer, para[iter].value);
		}
		printf("\n\n%s\n\n", api_buffer);
		
	}

	return api_buffer;
}
