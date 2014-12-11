
#define FRAME_TYPE_MANAGEMENT 0
#define FRAME_TYPE_CONTROL 1
#define FRAME_TYPE_DATA 2
#define FRAME_SUBTYPE_PROBE_REQUEST 0x04
#define FRAME_SUBTYPE_PROBE_RESPONSE 0x05
#define FRAME_SUBTYPE_BEACON 0x08
#define FRAME_SUBTYPE_AUTH 0x0b
#define FRAME_SUBTYPE_DEAUTH 0x0c
#define FRAME_SUBTYPE_DATA 0x14
typedef struct framectrl_80211
{
    //buf[0]
    uint8_t Protocol:2;
    uint8_t Type:2;
    uint8_t Subtype:4;
    //buf[1]
    uint8_t ToDS:1;
    uint8_t FromDS:1;
    uint8_t MoreFlag:1;
    uint8_t Retry:1;
    uint8_t PwrMgmt:1;
    uint8_t MoreData:1;
    uint8_t Protectedframe:1;
    uint8_t Order:1;
} framectrl_80211,*lpframectrl_80211;

typedef struct probe_request_80211
{
	struct framectrl_80211 framectrl;
	uint16_t duration;
	uint8_t rdaddr[6];
	uint8_t tsaddr[6];
	uint8_t bssid[6];
	uint16_t number;
} probe_request, *pprobe_request;

typedef struct tagged_parameter
{
	/* SSID parameter */
	uint8_t tag_number;
	uint8_t tag_length;
} tagged_parameter, *ptagged_parameter;