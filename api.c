#include "esp_common.h"
#include "api_struct.h"

extern char *strsep(char **stringp, const char *delim);

/*
	@param args: args string
	@param p: Params struct 

	@return int: number of parsed args
*/
int extract_params(char* args, Params *para)
{
	if (args == NULL)
		return 0;
	char *p = args;
	char *key_point;
	uint8_t count = 0;
	char *idx;
	char *saveptr;
	while(p)
	{
		/* if param number is bigger than MAX_PARAM, it returns */
		if (count > MAX_PARAM)
			break;
		while ( key_point = strsep(&p, "&"))
		{
			if (*key_point == 0)
				continue;
			else
				break;
		}
		/* deal with it */
		idx = strchr(key_point, '=');
		*idx = '\0';
		para->key = key_point;
		para->value = idx + 1;
		para++; /* next item */
		count++ ;
	}
	return count;
}
