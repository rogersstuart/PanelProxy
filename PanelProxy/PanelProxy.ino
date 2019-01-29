#include <SPI.h>
#include <EthernetUdp.h>
#include <EthernetServer.h>
#include <EthernetClient.h>
#include <Ethernet.h>
#include "MasterControllerV2_EVMOD_NETPORT.h"
#include <SoftwareSerial.h>
#include <Wire.h>
#include <EEPROM.h>
#include <avr/pgmspace.h>
#include <MqttClient.h>
#include <NTPClient.h>

const uint8_t master_ready = 'M';
const uint8_t master_acknowledge = 'A';
const uint8_t skip_panel_command = 'B';

const uint8_t reader_auth_codes[] = { 'D', 'E', 'F' };

const uint32_t read_timeout = 1000000; //in milliseconds
const uint32_t panel_scan_timeout = (1000000/8); //in milliseconds

uint32_t panel_scan_timer_0;
uint32_t panel_scan_timer_1;

uint32_t scan_timer;

//byte mac[] = { 0x77, 0xCC, 0xAC, 0x43, 0x98, 0xEA };

const byte mac[] = {
	0x00, 0xAA, 0xBB, 0xCC, 0xDE, 0x02
};
//IPAddress ntp_server_addr(192, 168, 0, 1); //local ntp server

//EthernetUDP ntp;
EthernetClient mqtt_client;

//NTPClient timeClient(ntp, "pfsense.mct");

//unsigned int ntp_port = 8888;  // local port to listen for NTP UDP packets
unsigned int config_listen_port = 44819;  // local port to listen for NTP UDP packets

const int timeZone = -6 * 60 * 60;     // CST (UTC diff) in seconds

MqttClient *mqtt = NULL;

const char* MQTT_TOPIC_PUB_0 = "acc/elev/0/rdr/tap";
const char* MQTT_TOPIC_PUB_1 = "acc/elev/1/rdr/tap";
const char* MQTT_TOPIC_SUB_0 = "acc/elev/0/rdr/tap/resp";
const char* MQTT_TOPIC_SUB_1 = "acc/elev/1/rdr/tap/resp";

panel_attrib out0 = { 0, 0, 0, 0, false };
panel_attrib out1 = { 1, 0, 0, 0, false };

void(*reset) (void) = 0;

class System : public MqttClient::System {
public:

	unsigned long millis() const {
		return ::millis();
	}
};

void setup()
{
	delay(2000);

	Serial.begin(MGMT_IFACE_DATA_RATE);

	Serial1.begin(PANEL_DATA_RATE);
	Serial2.begin(PANEL_DATA_RATE);

	Serial.println(F("init"));

	analogWrite(PNL_0_RED_LED, 2);
	analogWrite(PNL_1_RED_LED, 2);

	if (Ethernet.begin(mac) == 0)
		reset();

	Serial.println(F("eth up"));

	Serial.println(F("retrieving ntp time"));

	//ntp.begin(ntp_port);

	//timeClient.begin();
	//timeClient.setTimeOffset(timeZone);
	//timeClient.forceUpdate();

	//Serial.println(F("time set to"));

	//Serial.println(timeClient.getFormattedTime());

	// Setup MqttClient
	MqttClient::System *mqttSystem = new System;
	MqttClient::Logger *mqttLogger = new MqttClient::LoggerImpl<HardwareSerial>(Serial);
	MqttClient::Network * mqttNetwork = new MqttClient::NetworkClientImpl<EthernetClient>(mqtt_client, *mqttSystem);
	//// Make 128 bytes send buffer
	MqttClient::Buffer *mqttSendBuffer = new MqttClient::ArrayBuffer<128>();
	//// Make 128 bytes receive buffer
	MqttClient::Buffer *mqttRecvBuffer = new MqttClient::ArrayBuffer<128>();
	//// Allow up to 2 subscriptions simultaneously
	MqttClient::MessageHandlers *mqttMessageHandlers = new MqttClient::MessageHandlersImpl<2>();
	//// Configure client options
	MqttClient::Options mqttOptions;
	////// Set command timeout to 10 seconds
	mqttOptions.commandTimeoutMs = 10000;
	//// Make client object
	mqtt = new MqttClient(
		mqttOptions, *mqttLogger, *mqttSystem, *mqttNetwork, *mqttSendBuffer,
		*mqttRecvBuffer, *mqttMessageHandlers
	);
}

// ============== Subscription callback ========================================
void process_tap_0_resp(MqttClient::MessageData& md)
{
	const MqttClient::Message& msg = md.message;
	char payload[msg.payloadLen + 1];
	memcpy(payload, msg.payload, msg.payloadLen);

	out0.resp = (byte)payload[0];
	out0.resp_rdy = true;
}

void process_tap_1_resp(MqttClient::MessageData& md)
{
	const MqttClient::Message& msg = md.message;
	char payload[msg.payloadLen + 1];
	memcpy(payload, msg.payload, msg.payloadLen);

	out1.resp = (byte)payload[0];
	out1.resp_rdy = true;
}

void loop()
{

	int res = Ethernet.maintain();

	if (res != 0 && res != 2 && res != 4)
		reset();

	//timeClient.update();

	// Check connection status
	if (!mqtt->isConnected())
	{
		// Close connection if exists
		mqtt_client.stop();

		// Re-establish TCP connection with MQTT broker
		mqtt_client.connect("mccsrv1.mct", 1883);
		if (!mqtt_client.connected())
			reset();

		// Start new MQTT connection
		MqttClient::ConnectResult connectResult;
		// Connect
		{
			MQTTPacket_connectData options = MQTTPacket_connectData_initializer;
			options.MQTTVersion = 4;
			options.clientID.cstring = (char*)MQTT_ID;
			options.cleansession = true;
			options.keepAliveInterval = 15; // 15 seconds

			MqttClient::Error::type rc = mqtt->connect(options, connectResult);
			if (rc != MqttClient::Error::SUCCESS)
				reset();
		}

		MqttClient::Error::type rc = mqtt->subscribe
		(
			MQTT_TOPIC_SUB_0, MqttClient::QOS0, process_tap_0_resp
		);

		if (rc != MqttClient::Error::SUCCESS)
			reset();

		MqttClient::Error::type rc2 = mqtt->subscribe
		(
			MQTT_TOPIC_SUB_1, MqttClient::QOS0, process_tap_1_resp
		);

		if (rc2 != MqttClient::Error::SUCCESS)
			reset();
	}

	mqtt->yield(1);

	int i = 0;
	while (i < 2)
	{
		if ((uint32_t)((long)micros() - panel_scan_timer_0) >= PANEL_SCAN_INTERVAL)
		{
			//analogWrite(PNL_0_YELLOW_LED, 20);
			ps(&out0);
			//analogWrite(PNL_0_YELLOW_LED, 0);

			i++;

			panel_scan_timer_0 = micros();
		}

		if ((uint32_t)((long)micros() - panel_scan_timer_1) >= PANEL_SCAN_INTERVAL)
		{
			//analogWrite(PNL_1_YELLOW_LED, 20);
			ps(&out1);
			//analogWrite(PNL_1_YELLOW_LED, 0);

			i++;

			panel_scan_timer_1 = micros();
		}
	}
	
}

void ps(panel_attrib * attr)
{
	//if the panel is disconnected and the counter is greater than zero subtract one from the counter
	//if the panel is connected or the counter was not greater than zero then continue processing

	if (attr->disconnection_flag && attr->disconnection_counter > 0)
		attr->disconnection_counter--;
	else
	{
		HardwareSerial * port = attr->panel_id == 0 ? &Serial1 : &Serial2;
		int led = attr->panel_id == 0 ? PNL_0_YELLOW_LED : PNL_1_YELLOW_LED;

		analogWrite(led, 255);
		//purge the rx buffer in preperation for communication with the panel
		while (port->read() > -1);

		//send panel a master ready byte
		port->write(master_ready);

		analogWrite(led, 0);

		//wait for a response
		scan_timer = micros();
		while (!port->available())
			if ((uint32_t)((long)micros() - scan_timer) >= panel_scan_timeout)
				break;
			else
				delayMicroseconds(10);

		//if there was a response check to see if it is because of a card tap
		//if there was no response and the panel is disconnected set the counter to its maximum value
		//if the panel isn't disconnected then add one to the counter
		//if the counter has crossed the threshold set the disconnection flag to reduce the polling rate.

		if (port->available())
		{
			analogWrite(attr->panel_id == 0 ? PNL_0_RED_LED : PNL_1_RED_LED, 0);
			analogWrite(attr->panel_id == 0 ? PNL_0_GREEN_LED : PNL_1_GREEN_LED, 2);

			analogWrite(led, 255);

			if (port->read() != skip_panel_command)
			{
				analogWrite(led, 0);
				
				if (handleRequest(attr))
				{

					if (attr->disconnection_counter > 0)
						attr->disconnection_counter--;
					attr->disconnection_flag = false;
				}
				else
				{
					attr->disconnection_counter++;
					attr->disconnection_flag = true;

					analogWrite(attr->panel_id == 0 ? PNL_0_RED_LED : PNL_1_RED_LED, 2);
					analogWrite(attr->panel_id == 0 ? PNL_0_GREEN_LED : PNL_1_GREEN_LED, 0);
				}

				attr->disconnection_flag = true;
				attr->disconnection_counter = PANEL_DISCONNECTION_THRESHOLD;
			}

			analogWrite(led, 0);

		}
		else
			if (attr->disconnection_flag)
			{
				attr->disconnection_counter = PANEL_DISCONNECTION_THRESHOLD;

				analogWrite(attr->panel_id == 0 ? PNL_0_GREEN_LED : PNL_1_GREEN_LED, 0);
				analogWrite(attr->panel_id == 0 ? PNL_0_RED_LED : PNL_1_RED_LED, 2);
			}
			else
				if (++attr->disconnection_counter == PANEL_DISCONNECTION_THRESHOLD)
				{
					analogWrite(attr->panel_id == 0 ? PNL_0_GREEN_LED : PNL_1_GREEN_LED, 0);
					analogWrite(attr->panel_id == 0 ? PNL_0_RED_LED : PNL_1_RED_LED, 2);

					attr->disconnection_flag = true;
				}
	}
}

uint8_t handleRequest(panel_attrib * attr)
{
	HardwareSerial * panel_port = attr->panel_id == 0 ? &Serial1 : &Serial2;
	int led = attr->panel_id == 0 ? PNL_0_YELLOW_LED : PNL_1_YELLOW_LED;

	uint8_t uid_length, auth_code;
	uint32_t timeout_timer;

	analogWrite(led, 255);
	panel_port->write(master_acknowledge); //send master acknowledge
	analogWrite(led, 0);

	timeout_timer = micros();

	//wait to receive the uid length
	while (!panel_port->available())
		if ((uint32_t)((long)micros() - timeout_timer) >= read_timeout)
			return 0;
		else
		delayMicroseconds(10);

	analogWrite(led, 255);
	uid_length = panel_port->read(); //read the length of the uid

	panel_port->write(master_acknowledge); //send master acknowledge
	analogWrite(led, 0);

	uint8_t card_uid[8] = {0, 0, 0, 0, 0, 0, 0, 0};

	timeout_timer = micros();

	//wait for card id
	while (panel_port->available() < uid_length)
		if ((uint32_t)((long)micros() - timeout_timer) >= read_timeout)
			return 0;
		else
			delayMicroseconds(10);

	analogWrite(led, 255);
	//read card id into buffer
	for (uint8_t index_counter = 8-uid_length; index_counter < 8; index_counter++)
		card_uid[index_counter] = panel_port->read();
	analogWrite(led, 0);

	const char * topic = attr->panel_id == 0 ? MQTT_TOPIC_PUB_0 : MQTT_TOPIC_PUB_1;

	//generate mqtt message
	//String message_uc = String(timeClient.getEpochTime(), DEC) + ", " + String(uid_length, DEC) + ", ";
	//for (uint8_t index_counter = 0; index_counter < uid_length; index_counter++)
	//	message_uc += String(card_uid[index_counter], HEX);

	//char buffer[]

	///for (int i = 0; i < 4; i++)
	//	buffer[i] = card_uid[i];

	//sprintf(buffer, "%10lld", (*(uint64_t*)card_uid));

	//for (uint8_t index_counter = 0; index_counter < uid_length; index_counter++)
	//	*message_uc += String(card_uid[index_counter], HEX);

	//transmit message
	//const char* buf = card_uid;
	MqttClient::Message message;
	message.qos = MqttClient::QOS0;
	message.retained = false;
	message.dup = false;
	message.payload = (void*)card_uid;
	message.payloadLen = 8;
	mqtt->publish(topic, message);

	attr->resp_rdy = false;
	timeout_timer = micros();

	//yield until mqtt response or timeout
	while (!(attr->resp_rdy))
		if ((uint32_t)((long)micros() - timeout_timer) >= read_timeout)
			return 0;
		else
			mqtt->yield(1);

	auth_code = attr->resp;

	//send auth code to panel
	analogWrite(led, 255);
	panel_port->write(reader_auth_codes[auth_code]);
	analogWrite(led, 0);

	//disconnect panel because it won't respond for a period of time
	attr->disconnection_counter = PANEL_DISCONNECTION_THRESHOLD;
	attr->disconnection_flag = true;

	return 1;
}


