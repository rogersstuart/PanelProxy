#include <inttypes.h>

#define MGMT_IFACE_DATA_RATE 500000
#define PANEL_DATA_RATE 9600

#define PANEL_DISCONNECTION_THRESHOLD 16

#define PANEL_SCAN_INTERVAL (1000000/16)

#define PNL_0_RED_LED 4
#define PNL_1_RED_LED 7

#define PNL_0_GREEN_LED 3
#define PNL_1_GREEN_LED 6

#define PNL_0_YELLOW_LED 2
#define PNL_1_YELLOW_LED 5

#define UNLOCKED LOW 
#define LOCKED HIGH

#define MQTT_ID "ELE_PNL_RLY"

typedef struct panel_attrib
{
	uint8_t panel_id;
	uint8_t disconnection_counter;
	uint8_t disconnection_flag;
	byte resp;
	bool resp_rdy;
};
