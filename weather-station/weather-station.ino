
#include <STM32RTC.h>
#include <STM32LowPower.h>
#include <Wire.h>
#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <printf.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

// See README.md for wiring
#define RAIN_GAUGE_PIN   PA0   // Should be WKUP pin
#define BATT_VOLTAGE_PIN PA1   // Should be ADC pin
#define NRF24_CSN_PIN    PA4   // CSN pin for nRF24L01 radio
#define NRF24_CE_PIN     PB0   // CE pin for nRF24L01 radio
#define BME280_SCL_PIN   PB8   // SCL pin for BME280 sensor
#define BME280_SDA_PIN   PB9   // SDA pin for BME280 sensor
#define LED_BUILTIN      PB12  // LED on Black Pill

#define SLEEP_TIME       1 * 60  // Time between synchronizations (in seconds)


bool synchronize = true;

// nRF24L01 radio
RF24 radio(NRF24_CE_PIN, NRF24_CSN_PIN);

// BME280 Sensor
TwoWire Wire1(BME280_SDA_PIN, BME280_SCL_PIN);
Adafruit_BME280 bme;

// Rain Gauge
unsigned int bucket_tips = 0;
unsigned long last_bucket_tip = NULL; // for debouncing

// Real Time Clock
STM32RTC& rtc = STM32RTC::getInstance();

struct weather_data {
  float temperature;
  float pressure;
  float humidity;
  unsigned int bucket_tips;
  float battery_voltage;
};

const uint64_t pipes[2] = { 0xF0F0F0F0E1ULL, 0xF0F0F0F0D2ULL };


void setup() {
  synchronize = true;

  // UART Serial output for debugging
  Serial1.begin(115200);
  Serial1.println("Initializing...");

  // Real Time Clock
  rtc.begin(); // initialize RTC 24H format
  if (!rtc.isTimeSet()) {
    // Set the time
    rtc.setEpoch(1577836800, 0);
  }

  // Low Power
  LowPower.begin();
  LowPower.enableWakeupFrom(&rtc, woke_up_from_alarm);
  rtc.setAlarmEpoch(rtc.getEpoch() + SLEEP_TIME);

  // Rain gauge
  pinMode(RAIN_GAUGE_PIN, INPUT_PULLDOWN);
  attachInterrupt(digitalPinToInterrupt(RAIN_GAUGE_PIN), bucket_tipped, FALLING);
  LowPower.attachInterruptWakeup(digitalPinToInterrupt(RAIN_GAUGE_PIN), bucket_tipped, FALLING);

  // Battery Voltage
  analogReadResolution(ADC_RESOLUTION);
  pinMode(BATT_VOLTAGE_PIN, INPUT_ANALOG);

  // Setup NRF24 radio
  radio.begin();
  radio.setChannel(0);
  radio.setPALevel(RF24_PA_MAX);
  radio.setDataRate(RF24_250KBPS);
  radio.setAutoAck(false); // Does not work with my cheap NRF24 clones
  radio.enableDynamicPayloads();
  radio.setRetries(5, 15);
  radio.setCRCLength(RF24_CRC_16);

  radio.openWritingPipe(pipes[0]);
  radio.openReadingPipe(1, pipes[1]);

  radio.printDetails();

  // BME280
  unsigned status;
  byte i = 0;
  do {
    status = bme.begin(0x76, &Wire1);
    i++;
    delay(200);
  } while (i < 3 && !status);
  if (!status) {
    Serial1.println("ERROR: could not connect to BME280");
  }

  // Prevent the MCU to go to sleep in the first 5 seconds, this gives us some
  // time to flash the device in the first seconds after boot.
  // A workaround is to set BOOT0 pin to HIGH, then the sketch will not be
  // loaded and we can upload a new sketch.
  delay(5000);
}

void loop() {
  weather_data data;

  if (synchronize) {
    bme.takeForcedMeasurement();
    data.temperature = bme.readTemperature();
    data.pressure = bme.readPressure() / 100.0F;
    data.humidity = bme.readHumidity();

    data.bucket_tips = bucket_tips;
    data.battery_voltage = battery_voltage();

    sync_weather_data(data);

    synchronize = false;
  }

  if (!bucket_tipped_recently()) {
    last_bucket_tip = NULL;
    Serial1.println("Zzz");
    Serial1.flush();
    LowPower.deepSleep();
  }
}

void sync_weather_data(weather_data data) {
  Serial1.print("Temperature: "); // °C
  Serial1.println(data.temperature);
  Serial1.print("Pressure: "); // hPa
  Serial1.println(data.pressure);
  Serial1.print("Humidity: "); // %
  Serial1.println(data.humidity);
  Serial1.print("Bucket tip count: ");
  Serial1.println(data.bucket_tips);
  Serial1.print("Battery voltage: ");
  Serial1.println(data.battery_voltage);

  radio.powerUp();

  bool success = writeDataAck(radio, &data, sizeof(data));
  if (success) {
    Serial1.println("** Weather data sent");
  } else {
    Serial1.println("ERROR: Send failed");
  }

  radio.powerDown();
}

// I implement my own ACK protocol here, because the cheap NRF24 clones do not
// seem to have (working) auto ACK...
bool writeDataAck(RF24 &radio, const void *buf, uint8_t len) {
  char response[31];
  bool success = false;
  for (byte i=0; i < 5; ++i) {
    // Send data
    if (radio.write(buf, len)) {
      // Now listen for ACK
      radio.startListening();
      for (byte j=0; j < 10; ++j) {
        if (radio.available()) {
          len = radio.getDynamicPayloadSize();
          radio.read(&response, len);
          response[len] = '\0';
          if (strcmp(response, "ACK") == 0) {
            success = true;
            break;
          } else {
            // Got response but it's not ACK
            Serial1.print("Invalid ACK received: ");
            Serial1.println(response);
          }
        } else {
          // No ACK received
          delay(100);
        }
      }
      radio.stopListening();
      radio.flush_tx();
      radio.flush_rx();
    }
    if (success) break;
  }
  return success;
}

// Did the bucket tip in the last 500ms?
bool bucket_tipped_recently() {
  unsigned long now = millis();
  if (!last_bucket_tip) return false;
  if (last_bucket_tip > now) return false;
  if (now - last_bucket_tip > 500) return false;
  return true;
}

// Interrupt handler for rain bucket tip
void bucket_tipped() {
  // When we wake up from sleep to register a bucket tip, we don't want to
  // synchronize.
  // TODO: this seems to be getting called on cold boot too, figure out how to
  // fix that...
  synchronize = false;

  // Debounce
  if (bucket_tipped_recently()) return;
  last_bucket_tip = millis();

  Serial1.println("** Rain bucket tipped");

  bucket_tips += 1;
}

// Interrupt handler for alarm
void woke_up_from_alarm(void* data) {
  UNUSED(data);
  Serial1.println("** Woke up from alarm");

  synchronize = true;
  rtc.setAlarmEpoch(rtc.getEpoch() + SLEEP_TIME);
}

// Calculate the battery voltage
float battery_voltage() {
  const float adc_range = 2 << (ADC_RESOLUTION - 1);
  const float ref_voltage = 3.3;  // Reference voltage
  const float R1 = 100000;        // R1 of voltage divider (Ohm)
  const float R2 = 100000;        // R2 of voltage divider (Ohm)
  const float voltage_divider_multiplier = (R1 + R2) / R2;
  int x;

  for (byte i = 0; i < 3; ++i) {
    x = analogRead(BATT_VOLTAGE_PIN);
    Serial1.print("ADC: ");
    Serial1.println(x);
  }
  return (float(x) / (adc_range - 1)) * ref_voltage * voltage_divider_multiplier;
}
