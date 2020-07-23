
Weather Station
===============

The sensor data is processed by a STM32 Black Pill board (STM32F103C8T6). It
features a temperature, humidity and pressure sensor (BME280) and a self made
tipping bucket rain gauge (using a Reed switch and a magnet to detect the tips).
The weatherstation works on two 18650 batteries. The data is transmitted to a
Raspberry Pi using a 2.4GHz NRF24 radio, which then publishes the data to the
web.

I use the following hardware:

- STM32F1 MCU [Black Pill](https://stm32-base.org/boards/STM32F103C8T6-Black-Pill)
- BME280 temperature, humidity and pressure sensor
- Tipping bucket style rain gauge
- NRF24L01+ PA+LNA radio transmitter with antenna
- [18650 Battery
  Shield](https://www.diymore.cc/collections/battery-protection-board/products/18650-battery-shield-v8-mobile-power-bank-3v-5v-for-arduino-esp32-esp8266-wifi)
  and two 18650 Li-Ion batteries
- 6V 1W Solar Panel

Software:

- Arduino with STM32 Cores v1.9.0
- RF24 v1.3.6
- Adafruit BME280 Library v2.0.2
- STM32RTC v1.0.3
- STM32LowPower v1.0.3

Relay host:

- Raspberry Pi 3B
- NRF24L01 radio transmitter
- Relay server written in Python 3
- pyRF24 v1.3.6

Install
-------

### Arduino

Install Arduino IDE, for example on Fedora:

```bash
sudo dnf install arduino
```

Install [STM32 Cores](https://github.com/stm32duino/wiki/wiki/Getting-Started)
(using Board Manager).

Install [STM32RTC](https://github.com/stm32duino/STM32RTC) in your
`Arduino/libraries` folder.

Install [STM32LowPower](https://github.com/stm32duino/STM32LowPower) in your
`Arduino/libraries` folder.

Install [RF24](https://github.com/nRF24/RF24) in your `Arduino/libraries`
folder.

Install [Adafruit BME280](https://github.com/adafruit/Adafruit_BME280_Library)
(using Library Manager).

Install [STM32 Cube
Programmer](https://www.st.com/en/development-tools/stm32cubeprog.html) to
upload the sketch to the MCU.

### Raspberry Pi

Copy the directory `weather_station_relay` to a directory on your Pi and `cd`
into it, then execute the following commands:

```bash
virtualenv -p python3 venv
. venv/bin/activate
git clone https://github.com/nRF24/RF24.git
cd RF24
make
cd pyRF24
python setup.py install
cd ../..
```

To run the server:

```bash
sudo python main.py
```

Wiring
------

| STM32 | Device     | Port            |
|-------|------------|-----------------|
| PA0   | Rain Gauge | Reed switch     |
| PA1   | Battery    | Voltage Divider |
| PB0   | NRF24L01   | CE              |
| PA4   | NRF24L01   | CSN             |
| PA5   | NRF24L01   | SCLK            |
| PA6   | NRF24L01   | MISO            |
| PA7   | NRF24L01   | MOSI            |
| PA9   | FTDI       | RXD             |
| PA10  | FTDI       | TXD             |
| PB8   | BME280     | SCK/SCL         |
| PB9   | BME280     | SDI/SDA         |

For the voltage divider I've used two 100kΩ resistors with a 0.1μF ceramic
capacitor parallel to ground. Solder R1 to one of the two positive connectors on
the battery shield, and R2 and the capacitor to ground, the other end goes to
the *PA1* port of the STM32. See the article [Measuring the battery without
draining
it](https://jeelabs.org/2013/05/16/measuring-the-battery-without-draining-it/)
for details.

As recommended I've soldered 100μF electrolytic capacitors between *VCC* and
*GND* on the NRF24L01 modules, minus of the capacitor to *GND*.

I've put a [micro USB
connector](https://www.aliexpress.com/item/32835384598.html) on the cable to the
solar panel that goes directly into the input of the 18650 battery shield.

For the tipping bucket rain gauge I used a reed switch that connects port *PA0*
to *VCC* on the STM32. The reed switch is closed when the magnet on the tipping
bucket rain gauge moves across it.

Serial monitor
--------------

I use a [FT232 FTDI device](https://www.aliexpress.com/item/32857360219.html) to
read the serial output on my PC.

Open serial monitor with 115200 baud on /dev/ttyUSB0 or /dev/ttyUSB1. On Linux
you can use screen:

```bash
screen -L -Logfile stm32.log /dev/ttyUSB0 115200
```

Note: *TX* and *RX* are reverse connected. Don't forget to connect *GND* of the
FTDI to the *GND* of your STM32 (no need to connect *VCC*).

TODO
----

- The battery measurement seems to be off.
