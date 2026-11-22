#include <Arduino.h>
#include <Zigbee.h>
#include <Wire.h>
#include <SensirionI2cSht4x.h>

#define NO_ERROR 0

#ifndef ZIGBEE_MODE_ZCZR
#error "Zigbee coordinator mode is not selected in Tools->Zigbee mode"
#endif

namespace config
{
  constexpr uint8_t TEMPERATURE_ENDPOINT = 10;
  constexpr uint32_t SERIAL_BAUD_RATE = 115200;
  constexpr TickType_t SENSOR_READ_INTERVAL_MS = pdMS_TO_TICKS(10000);
  constexpr TickType_t LOOP_DELAY_MS = pdMS_TO_TICKS(100);
  constexpr uint32_t SENSOR_TASK_STACK_SIZE = 3072;
  constexpr UBaseType_t SENSOR_TASK_PRIORITY = 10;
  constexpr uint8_t LED_ON = LOW;
  constexpr uint8_t LED_OFF = HIGH;
  constexpr uint16_t SERIAL_DELAY_MS = 2000;
} // namespace config

namespace
{
  uint8_t button = BOOT_PIN;
  ZigbeeTempSensor zbTempSensor(config::TEMPERATURE_ENDPOINT);
  SensirionI2cSht4x sensor;

  void blinkLEDonIdentify(uint16_t identifySeconds)
  {
    // Blink the LED for the duration of identifySeconds
    // this function is called every second by the Zigbee stack during the identify process
    if (identifySeconds == 0) return;
    digitalWrite(LED_BUILTIN, config::LED_ON);
    vTaskDelay(pdMS_TO_TICKS(500));
    digitalWrite(LED_BUILTIN, config::LED_OFF);
    vTaskDelay(pdMS_TO_TICKS(500));
  }

  void printSensorError(const char *operation, int16_t error)
  {
    char errorMessage[64];
    Serial.printf("Error trying to execute %s(): ", operation);
    errorToString(error, errorMessage, sizeof(errorMessage));
    Serial.println(errorMessage);
  }

  void rebootAfterZigbeeError(const char *message)
  {
    Serial.println(message);
    Serial.println("Rebooting...");
    delay(1000);
    ESP.restart();
  }

  void initializeZigbee()
  {
    if (!Zigbee.role(ZIGBEE_ROUTER))
    {
      rebootAfterZigbeeError("Zigbee failed to init!");
    }

    zbTempSensor.setManufacturerAndModel("DIY", "ESP-32 Temp and Hum Sensor");
    zbTempSensor.setMinMaxValue(-20, 40);
    zbTempSensor.setDefaultValue(-1);
    zbTempSensor.setTolerance(0.1);
    zbTempSensor.addHumiditySensor(0, 100, 0.1, 1);
    zbTempSensor.addTimeCluster();
    zbTempSensor.onIdentify(blinkLEDonIdentify);
    Zigbee.addEndpoint(&zbTempSensor);

    Serial.println("Starting Zigbee...");
    if (!Zigbee.begin())
    {
      rebootAfterZigbeeError("Zigbee failed to start!");
    }

    Serial.println("Zigbee started successfully!");
    zbTempSensor.setPowerSource(ZB_POWER_SOURCE_MAINS);

    Serial.println("Connecting to network");
    while (!Zigbee.connected())
    {
      Serial.print(".");
      delay(100);
    }
    Serial.println();

    zbTempSensor.registerTimeServer();
    struct tm timeinfo = zbTempSensor.getTime();
    int32_t timezone = zbTempSensor.getTimezone();
    time_t local = mktime(&timeinfo) + timezone;
    struct tm *localTime = localtime(&local);

    Serial.println("Local time with timezone:");
    Serial.println(localTime, "%A, %B %d %Y %H:%M:%S");

    zbTempSensor.setReporting(1, 60, 0.1);
  }

  void initializeSht4x()
  {
    Wire.begin();
    sensor.begin(Wire, SHT40_I2C_ADDR_44);
    sensor.softReset();
    delay(10);

    uint32_t serialNumber = 0;
    int16_t error = sensor.serialNumber(serialNumber);
    if (error != NO_ERROR)
    {
      printSensorError("serialNumber", error);
      ESP.restart();
    }

    Serial.print("serialNumber: ");
    Serial.println(serialNumber);
  }

  void serialDelay() {
    digitalWrite(LED_BUILTIN, config::LED_ON);

    TickType_t startTime = xTaskGetTickCount();
    const TickType_t timeout = pdMS_TO_TICKS(config::SERIAL_DELAY_MS);
    while (!Serial && (xTaskGetTickCount() - startTime) < timeout)
    {
      vTaskDelay(pdMS_TO_TICKS(10));
    }

    digitalWrite(LED_BUILTIN, config::LED_OFF);
  }

  void tempSensorValueUpdate(void *arg)
  {
    (void)arg;
    TickType_t lastWakeTime = xTaskGetTickCount();

    for (;;)
    {
      float temperature = 0.0;
      float humidity = 0.0;

      Serial.println("Reporting current readings...");
      int16_t error = sensor.measureHighPrecision(temperature, humidity);
      if (error != NO_ERROR)
      {
        printSensorError("measureHighPrecision", error);
      }
      else
      {
        zbTempSensor.setTemperature(temperature);
        zbTempSensor.setHumidity(humidity);
        Serial.printf("%f°C\t%f%% rH\n", temperature, humidity);
      }

      vTaskDelayUntil(&lastWakeTime, config::SENSOR_READ_INTERVAL_MS);
    }
  }

  void handleButton()
  {
    if (digitalRead(button) != LOW)
    {
      return;
    }

    delay(100);
    uint32_t pressStartTime = millis();

    while (digitalRead(button) == LOW)
    {
      delay(50);
      if ((millis() - pressStartTime) > 3000)
      {
        Serial.println("Resetting Zigbee to factory and rebooting in 1s.");
        delay(1000);
        Zigbee.factoryReset();
      }
    }

    Serial.println("Reporting current data");
    zbTempSensor.reportTemperature();
  }
} // namespace

void setup()
{
  Serial.begin(config::SERIAL_BAUD_RATE);

  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(button, INPUT_PULLUP);
  digitalWrite(LED_BUILTIN, config::LED_OFF);

  serialDelay();
  initializeZigbee();
  initializeSht4x();

  xTaskCreate(tempSensorValueUpdate,
              "temp_sensor_update",
              config::SENSOR_TASK_STACK_SIZE,
              NULL,
              config::SENSOR_TASK_PRIORITY,
              NULL);
}

void loop()
{
  handleButton();
  vTaskDelay(config::LOOP_DELAY_MS);
}
