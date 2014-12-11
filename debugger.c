#include "c_types.h"
#include "osapi.h"

void ICACHE_FLASH_ATTR
print_data(uint8_t* buf, uint16_t len)
{
	uint16_t i;
	uint8_t *ptr = buf;
	for(i = 0; i < len; i++)
	{
		os_printf("%02x ", *ptr++);
	}
	os_printf("\n");
}
