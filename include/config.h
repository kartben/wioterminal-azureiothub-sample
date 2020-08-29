//#define USE_DPS

// Wi-Fi
#define IOT_CONFIG_WIFI_SSID				"[wifi ssid]"
#define IOT_CONFIG_WIFI_PASSWORD			"[wifi password]"

#if !defined(USE_DPS)
// Azure IoT Hub
#define IOT_CONFIG_IOTHUB					"[Azure IoT Hub host name].azure-devices.net"
#define IOT_CONFIG_DEVICE_ID				"[device id]"
#define IOT_CONFIG_SYMMETRIC_KEY			"[symmetric key]"
#define IOT_CONFIG_MODEL_ID					"dtmi:seeedkk:wioterminal;1"
#else
// Azure IoT Hub DPS
#define IOT_CONFIG_GLOBAL_DEVICE_ENDPOINT	"global.azure-devices-provisioning.net"
#define IOT_CONFIG_ID_SCOPE					"[id scope]"
#define IOT_CONFIG_REGISTRATION_ID			"[device id]"
#define IOT_CONFIG_SYMMETRIC_KEY			"[symmetric key]"
#define IOT_CONFIG_MODEL_ID					"dtmi:seeedkk:wioterminal;1"
#endif // USE_DPS

#define TELEMETRY_FREQUENCY_MILLISECS		2000
#define TELEMETRY_ACCEL						"accel"
#define COMMAND_RING_BUZZER					"ringBuzzer"
