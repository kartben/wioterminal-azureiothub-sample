#include <Arduino.h>
#include "config.h"
#include "az_iot_helpers.h"

#include <WiFiClientSecure.h>
#include <PubSubClient.h>

#include "WiFiUdp.h"
#include "NTP.h"

#include <az_json.h>
#include <az_result.h>
#include <az_span.h>
#include <az_iot_hub_client.h>

#include "mbedtls/md.h"
#include "mbedtls/base64.h"
#include "mbedtls/sha256.h"

#include "LIS3DHTR.h"
LIS3DHTR<TwoWire> lis;

#include "TFT_eSPI.h"
#include "Free_Fonts.h" //include the header file
TFT_eSPI tft;
int current_text_line = 0;
#define LCD_WIDTH 320
#define LCD_HEIGHT 240
#define LCD_FONT FreeSans9pt7b
#define LCD_LINE_HEIGHT 18

#define sizeofarray(a) (sizeof(a) / sizeof(a[0]))
#define MQTT_PACKET_SIZE 1024

const char *ssid = IOT_CONFIG_WIFI_SSID;
const char *password = IOT_CONFIG_WIFI_PASSWORD;

const char *server = IOT_CONFIG_IOTHUB_FQDN;
const char *deviceId = IOT_CONFIG_DEVICE_ID;
const char *deviceKey = IOT_CONFIG_DEVICE_KEY;

static const az_span model_id = AZ_SPAN_LITERAL_FROM_STR("dtmi:seeed:wioterminal;1");

// Publish 1 message every 2 seconds
#define TELEMETRY_FREQUENCY_MILLISECS 2000

static az_iot_hub_client iot_hub_client;
static bool is_ready_to_send = false;
static char sas_token[300];
static unsigned long next_telemetry_send_time_ms = 0;

static char telemetry_topic[128];

static const az_span ringBuzzer_command_name_span = AZ_SPAN_LITERAL_FROM_STR("ringBuzzer");
static char commands_response_topic[128];
static char commands_response_payload[256];

static const az_span imu_telemetry_name = AZ_SPAN_LITERAL_FROM_STR("imu");
static char telemetry_payload[80];

static const az_span empty_payload = AZ_SPAN_LITERAL_FROM_STR("{}");

const char *baltimore_root_ca =
"-----BEGIN CERTIFICATE-----\n"
"MIIDdzCCAl+gAwIBAgIEAgAAuTANBgkqhkiG9w0BAQUFADBaMQswCQYDVQQGEwJJ\n"
"RTESMBAGA1UEChMJQmFsdGltb3JlMRMwEQYDVQQLEwpDeWJlclRydXN0MSIwIAYD\n"
"VQQDExlCYWx0aW1vcmUgQ3liZXJUcnVzdCBSb290MB4XDTAwMDUxMjE4NDYwMFoX\n"
"DTI1MDUxMjIzNTkwMFowWjELMAkGA1UEBhMCSUUxEjAQBgNVBAoTCUJhbHRpbW9y\n"
"ZTETMBEGA1UECxMKQ3liZXJUcnVzdDEiMCAGA1UEAxMZQmFsdGltb3JlIEN5YmVy\n"
"VHJ1c3QgUm9vdDCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAKMEuyKr\n"
"mD1X6CZymrV51Cni4eiVgLGw41uOKymaZN+hXe2wCQVt2yguzmKiYv60iNoS6zjr\n"
"IZ3AQSsBUnuId9Mcj8e6uYi1agnnc+gRQKfRzMpijS3ljwumUNKoUMMo6vWrJYeK\n"
"mpYcqWe4PwzV9/lSEy/CG9VwcPCPwBLKBsua4dnKM3p31vjsufFoREJIE9LAwqSu\n"
"XmD+tqYF/LTdB1kC1FkYmGP1pWPgkAx9XbIGevOF6uvUA65ehD5f/xXtabz5OTZy\n"
"dc93Uk3zyZAsuT3lySNTPx8kmCFcB5kpvcY67Oduhjprl3RjM71oGDHweI12v/ye\n"
"jl0qhqdNkNwnGjkCAwEAAaNFMEMwHQYDVR0OBBYEFOWdWTCCR1jMrPoIVDaGezq1\n"
"BE3wMBIGA1UdEwEB/wQIMAYBAf8CAQMwDgYDVR0PAQH/BAQDAgEGMA0GCSqGSIb3\n"
"DQEBBQUAA4IBAQCFDF2O5G9RaEIFoN27TyclhAO992T9Ldcw46QQF+vaKSm2eT92\n"
"9hkTI7gQCvlYpNRhcL0EYWoSihfVCr3FvDB81ukMJY2GQE/szKN+OMY3EU/t3Wgx\n"
"jkzSswF07r51XgdIGn9w/xZchMB5hbgF/X++ZRGjD8ACtPhSNzkE1akxehi/oCr0\n"
"Epn3o0WC4zxe9Z2etciefC7IpJ5OCBRLbf1wbWsaY71k5h+3zvDyny67G7fyUIhz\n"
"ksLi4xaNmjICq44Y3ekQEe5+NauQrz4wlHrQMz2nZQ/1/I6eYs9HRCwBXbsdtTLS\n"
"R9I4LtD+gdwyah617jzV/OeBHRnDJELqYzmp\n"
"-----END CERTIFICATE-----";

WiFiClientSecure wifi_client;
PubSubClient mqtt_client(wifi_client);

WiFiUDP wifiUdp;
NTP ntp(wifiUdp);

void lcd_log_line(char* line) {
    // clear line
    tft.fillRect(0, current_text_line * LCD_LINE_HEIGHT, 320, LCD_LINE_HEIGHT, TFT_WHITE);
    tft.drawString(line, 5, current_text_line * LCD_LINE_HEIGHT);

    current_text_line++;
    current_text_line %= ((LCD_HEIGHT-20)/LCD_LINE_HEIGHT);
}

// Send the response of the command invocation
static int send_command_response(
    az_iot_hub_client_method_request *request,
    uint16_t status,
    az_span response)
{
    az_result rc;
    // Get the response topic to publish the command response
    if (az_failed(
        rc = az_iot_hub_client_methods_response_get_publish_topic(
            &iot_hub_client,
            request->request_id,
            status,
            commands_response_topic,
            sizeof(commands_response_topic),
            NULL)))
    {
        Serial.println("Unable to get method response publish topic");
        return rc;
    }

    Serial.printf("Status: %u\tPayload: '", status);
    char *payload_char = (char *)az_span_ptr(response);
    if (payload_char != NULL)
    {
        for (int32_t i = 0; i < az_span_size(response); i++)
        {
            Serial.print(*(payload_char + i));
        }
    }
    Serial.println("'\n");

    // Send the commands response
    if (mqtt_client.publish(commands_response_topic, az_span_ptr(response), az_span_size(response), false))
    {
        Serial.println("Sent response");
    }

    return rc;
}

// Invoke the requested command if supported and return status | Return error otherwise
static void handle_command_message(
    az_span payload,
    az_iot_hub_client_method_request *command_request)
{
    int command_res_code = 200;
    az_result res;

    if (az_span_is_content_equal(ringBuzzer_command_name_span, command_request->name))
    {
        // Parse the command payload (it contains a 'duration' field)
        Serial.println("Processing command 'ringBuzzer'");
        char buffer[32];
        az_span_to_str(buffer, 32, payload);
        Serial.printf("Raw command payload: %s\r\n", buffer);

        az_json_reader json_reader;
        uint32_t duration;
        if (az_json_reader_init(&json_reader, payload, NULL) == AZ_OK)
        {
            if (az_json_reader_next_token(&json_reader) == AZ_OK)
            {
                if ((res = az_json_token_get_uint32(&json_reader.token, &duration)) == AZ_OK)
                {
                    Serial.printf("Duration: %dms\r\n", duration);
                }
                else
                {
                    Serial.printf("Couldn't parse JSON token res=%d\r\n", res);
                }
            }

            // Invoke command
            analogWrite(WIO_BUZZER, 128);
            delay(duration);
            analogWrite(WIO_BUZZER, 0);

            int rc;
            if ((rc = send_command_response(command_request, command_res_code, empty_payload)) != 0)
            {
                printf("Unable to send %d response, status %d\n", command_res_code, rc);
            }
        }
    }
    else
    {
        // Unsupported command
        Serial.printf(
            "Unsupported command received: %.*s.\n",
            az_span_size(command_request->name),
            az_span_ptr(command_request->name));

        int rc;
        if ((rc = send_command_response(command_request, 404, empty_payload)) != 0)
        {
            printf("Unable to send %d response, status %d\n", 404, rc);
        }
    }
}

void callback(char *topic, byte *payload, unsigned int length)
{
    az_span topic_span = az_span_create((uint8_t *)topic, strlen(topic));
    az_iot_hub_client_method_request command_request;

    if (az_succeeded(az_iot_hub_client_methods_parse_received_topic(
        &iot_hub_client, topic_span, &command_request)))
    {
        lcd_log_line("Command arrived!");
        Serial.println("Command arrived!");
        // Determine if the command is supported and take appropriate actions
        handle_command_message(az_span_create(payload, length), &command_request);
    }

    Serial.println();
}

static int connectToAzureIoTHub(const az_iot_hub_client *iot_hub_client)
{
    size_t client_id_length;
    char mqtt_client_id[128];
    if (az_failed(az_iot_hub_client_get_client_id(
        iot_hub_client, mqtt_client_id, sizeof(mqtt_client_id) - 1, &client_id_length)))
    {
        Serial.println("Failed getting client id");
        return 1;
    }

    mqtt_client_id[client_id_length] = '\0';

    char mqtt_username[256];
    // Get the MQTT user name used to connect to IoT Hub
    if (az_failed(az_iot_hub_client_get_user_name(
        iot_hub_client, mqtt_username, sizeofarray(mqtt_username), NULL)))
    {
        printf("Failed to get MQTT clientId, return code\n");
        return 1;
    }

    while (!mqtt_client.connected())
    {
        lcd_log_line("Connecting to Azure IoT Hub...");
        Serial.println("Connecting to Azure IoT Hub...");


        if (mqtt_client.connect(mqtt_client_id, mqtt_username, sas_token))
        {
            lcd_log_line("> SUCCESS.");
            Serial.println("> SUCCESS.");
        }
        else
        {
            lcd_log_line("> ERROR.");
            Serial.printf("> ERROR. Status code =%d. Try again in 5 seconds.\r\n",mqtt_client.state());
            // Wait 5 seconds before retrying
            delay(5000);
        }
    }

    // Subscribe to the commands topic.
    mqtt_client.subscribe(AZ_IOT_HUB_CLIENT_METHODS_SUBSCRIBE_TOPIC);

    // Subscribe to the Cloud-to-Device topic.
    mqtt_client.subscribe(AZ_IOT_HUB_CLIENT_C2D_SUBSCRIBE_TOPIC);

    return 0;
}

void setup()
{
    tft.begin();
    tft.setRotation(3);
    tft.fillScreen(TFT_WHITE);
    tft.setFreeFont(&LCD_FONT);
    tft.setTextColor(TFT_BLACK);

    pinMode(WIO_BUZZER, OUTPUT);

    //Initialize serial
    Serial.begin(115200);
    
    // Init IMU
    lis.begin(Wire1);
    lis.setOutputDataRate(LIS3DHTR_DATARATE_25HZ); // Setting output data rage to 25Hz, can be set up tp 5kHz
    lis.setFullScaleRange(LIS3DHTR_RANGE_2G);      // Setting scale range to 2g, select from 2,4,8,16g

    char buf[42];
    sprintf(buf, "Connecting to SSID: %s", ssid);
    lcd_log_line(buf);
    Serial.println(buf);
    WiFi.begin(ssid, password);

    // attempt to connect to Wifi network:
    while (WiFi.status() != WL_CONNECTED)
    {
        Serial.print(".");
        // wait 1 second for re-trying
        delay(1000);
    }
    lcd_log_line("> SUCCESS.");
    Serial.println("\r\n> SUCCESS.");

    wifi_client.setCACert(baltimore_root_ca);

    mqtt_client.setServer(server, 8883);
    mqtt_client.setCallback(callback);
    mqtt_client.setBufferSize(MQTT_PACKET_SIZE); // This is important! The default buffer size of the PubSubClient MQTT library is too small for e.g. the connection packets.

    // Initialize the Azure IoT Hub client with the hub host endpoint and the default connection options
    az_iot_hub_client_options options = az_iot_hub_client_options_default();
    // This is optional, but allows to leverage IoT Plug and Play
    options.model_id = model_id;

    if (az_failed(az_iot_hub_client_init(
        &iot_hub_client,
        az_span_create((uint8_t *)server, strlen(server)),
        az_span_create((uint8_t *)deviceId, strlen(deviceId)),
        &options)))
    {
        Serial.println("Failed initializing Azure IoT Hub client");
        return;
    }

    ntp.begin();

    // The SAS token is valid for 1 hour by default.
    uint32_t expiration = ntp.epoch() + 3600;
    // After one hour the sample must be restarted, or the client won't be able
    // to connect/stay connected to the Azure IoT Hub.
    if (generateSasToken(&iot_hub_client, deviceKey, expiration, sas_token, sizeofarray(sas_token)) != 0)
    {
        Serial.println("Failed generating MQTT password");
    }
    else if (connectToAzureIoTHub(&iot_hub_client) == 0)
    {
        is_ready_to_send = true;
    }
}

static az_result send_telemetry()
{
    az_json_writer json_builder;

    if (az_failed(az_iot_hub_client_telemetry_get_publish_topic(
        &iot_hub_client, NULL, telemetry_topic, sizeof(telemetry_topic), NULL)))
    {
        Serial.println("Failed az_iot_hub_client_telemetry_get_publish_topic");
        return AZ_ERROR_NOT_SUPPORTED;
    }

    float acc_x, acc_y, acc_z;
    lis.getAcceleration(&acc_x, &acc_y, &acc_z);

    AZ_RETURN_IF_FAILED(
        az_json_writer_init(&json_builder, AZ_SPAN_FROM_BUFFER(telemetry_payload), NULL));
    AZ_RETURN_IF_FAILED(az_json_writer_append_begin_object(&json_builder));
    AZ_RETURN_IF_FAILED(az_json_writer_append_property_name(&json_builder, imu_telemetry_name));
    AZ_RETURN_IF_FAILED(az_json_writer_append_begin_object(&json_builder));
    AZ_RETURN_IF_FAILED(az_json_writer_append_property_name(&json_builder, AZ_SPAN_LITERAL_FROM_STR("x")));
    AZ_RETURN_IF_FAILED(az_json_writer_append_double(&json_builder, acc_x, 3));
    AZ_RETURN_IF_FAILED(az_json_writer_append_property_name(&json_builder, AZ_SPAN_LITERAL_FROM_STR("y")));
    AZ_RETURN_IF_FAILED(az_json_writer_append_double(&json_builder, acc_y, 3));
    AZ_RETURN_IF_FAILED(az_json_writer_append_property_name(&json_builder, AZ_SPAN_LITERAL_FROM_STR("z")));
    AZ_RETURN_IF_FAILED(az_json_writer_append_double(&json_builder, acc_z, 3));
    AZ_RETURN_IF_FAILED(az_json_writer_append_end_object(&json_builder));
    AZ_RETURN_IF_FAILED(az_json_writer_append_end_object(&json_builder));
    az_span out_payload;
    out_payload = az_json_writer_get_bytes_used_in_destination(&json_builder);

    mqtt_client.publish(telemetry_topic, az_span_ptr(out_payload), az_span_size(out_payload), false);

    return AZ_OK;
}

void loop()
{
    ntp.update();

    if (millis() > next_telemetry_send_time_ms)
    {
        if (is_ready_to_send)
        {
            send_telemetry();
        }

        mqtt_client.loop();
        next_telemetry_send_time_ms = millis() + TELEMETRY_FREQUENCY_MILLISECS;
    }
}