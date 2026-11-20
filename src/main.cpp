#include <Arduino.h>
#include <Zigbee.h>
#include <Wire.h>
#include <SensirionI2cSht4x.h>

#define NO_ERROR 0

#ifndef ZIGBEE_MODE_ZCZR
#error "Zigbee coordinator mode is not selected in Tools->Zigbee mode"
#endif

/* Zigbee temperature sensor configuration */
#define TEMP_SENSOR_ENDPOINT_NUMBER 10
uint8_t button = BOOT_PIN;

// Optional Time cluster variables
struct tm timeinfo;
struct tm *localTime;
int32_t timezone;

ZigbeeTempSensor zbTempSensor = ZigbeeTempSensor(TEMP_SENSOR_ENDPOINT_NUMBER);

// SHT4x Sensor
SensirionI2cSht4x sensor;
static char errorMessage[64];
static int16_t error;

static void temp_sensor_value_update(void *arg)
{

  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xFrequency = pdMS_TO_TICKS(10000);

  for (;;)
  {
    float aTemperature = 0.0;
    float aHumidity = 0.0;

    Serial.println("Reporting current readings...");
    error = sensor.measureHighPrecision(aTemperature, aHumidity);
    if (error != NO_ERROR)
    {
      Serial.print("Error trying to execute measureHighPrecision(): ");
      errorToString(error, errorMessage, sizeof errorMessage);
      Serial.println(errorMessage);
    } else {
      zbTempSensor.setTemperature(aTemperature);
      zbTempSensor.setHumidity(aHumidity);

      Serial.printf("%f°C\t%f%% rH\n", aTemperature, aHumidity);
    }

    vTaskDelayUntil(&xLastWakeTime, xFrequency);
  }
}

void setup()
{
  Serial.begin(115200);

  // Zigbee Init
  // Initialize Zigbee stack as router
  if (!Zigbee.role(ZIGBEE_ROUTER))
  {
    Serial.println("Zigbee failed to init!");
    Serial.println("Rebooting...");
    delay(1000);
    ESP.restart();
  }

  // Init button switch
  pinMode(button, INPUT_PULLUP);
  zbTempSensor.setManufacturerAndModel("DIY", "ESP-32 Temp and Hum Sensor");
  zbTempSensor.setMinMaxValue(-20, 40);
  zbTempSensor.setDefaultValue(-1);
  zbTempSensor.setTolerance(0.1);

  zbTempSensor.addHumiditySensor(0, 100, 0.1, 1);
  zbTempSensor.addTimeCluster();

  Zigbee.addEndpoint(&zbTempSensor);

  Serial.println("Starting Zigbee...");
  // When all EPs are registered, start Zigbee
  if (!Zigbee.begin())
  {
    Serial.println("Zigbee failed to start!");
    Serial.println("Rebooting...");
    ESP.restart();
  }
  else
  {
    Serial.println("Zigbee started successfully!");
  }

  zbTempSensor.setPowerSource(ZB_POWER_SOURCE_MAINS);

  Serial.println("Connecting to network");
  while (!Zigbee.connected())
  {
    Serial.print(".");
    delay(100);
  }
  Serial.println();

  // Time setup
  zbTempSensor.registerTimeServer();
  timeinfo = zbTempSensor.getTime();
  timezone = zbTempSensor.getTimezone();

  time_t local = mktime(&timeinfo) + timezone;
  localTime = localtime(&local);

  Serial.println("Local time with timezone:");
  Serial.println(localTime, "%A, %B %d %Y %H:%M:%S");

  zbTempSensor.setReporting(1, 60, 0.1);

  // SHT4x Init
  Wire.begin();
  sensor.begin(Wire, SHT40_I2C_ADDR_44);
  sensor.softReset();
  delay(10);
  uint32_t serialNumber = 0;
  error = sensor.serialNumber(serialNumber);
  if (error != NO_ERROR)
  {
    Serial.print("Error trying to execute serialNumber(): ");
    errorToString(error, errorMessage, sizeof errorMessage);
    Serial.println(errorMessage);
    return;
  }
  Serial.print("serialNumber: ");
  Serial.print(serialNumber);
  Serial.println();

  // Start Temperature sensor reading task
  xTaskCreate(temp_sensor_value_update, "temp_sensor_update", 3072, NULL, 10, NULL);
}

void loop()
{

  // Checking button for factory reset
  if (digitalRead(button) == LOW)
  { // Push button pressed
    // Key debounce handling
    delay(100);
    int startTime = millis();
    while (digitalRead(button) == LOW)
    {
      delay(50);
      if ((millis() - startTime) > 3000)
      {
        // If key pressed for more than 3secs, factory reset Zigbee and reboot
        Serial.println("Resetting Zigbee to factory and rebooting in 1s.");
        delay(1000);
        Zigbee.factoryReset();
      }
    }
    Serial.println("Reporting current data");
    zbTempSensor.reportTemperature();
  }
  vTaskDelay(pdMS_TO_TICKS(100));
}
