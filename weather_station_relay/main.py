#!/usr/bin/env python
import datetime
import math
import re
import shelve
import struct
import time
import apie

from RF24 import *

# Rain gauge
RAIN_GAUGE_DIAMETER = 20.2        # diameter of funnel in cm
RAIN_GAUGE_BUCKET_CONTENTS = 1.8  # bucket contents in cm³ / milliliter

# C struct sent from MCU
# https://docs.python.org/3/library/struct.html#byte-order-size-and-alignment
WeatherData = struct.Struct(format="<3fIf")

# NRF24L01 radio
pipes = [0xF0F0F0F0E1, 0xF0F0F0F0D2]
radio = RF24(25, 0)  # CE Pin, CSN Pin


def get_bucket_tips_today(bucket_tips):
    """
    Because the MCU does not keep track of the day/time, we keep the number of
    bucket tips at the end of the previous day, and we subtract that number from
    the number of bucket tips the MCU reported.
    """
    today = datetime.date.today()
    yesterday = today - datetime.timedelta(days=1)
    with shelve.open('bucket_tips') as db:
        if yesterday.isoformat() in db:
            prev_bucket_tips = db[yesterday.isoformat()]
        else:
            prev_bucket_tips = None
        db[today.isoformat()] = bucket_tips
    if prev_bucket_tips is None or prev_bucket_tips > bucket_tips:
        return bucket_tips
    else:
        return bucket_tips - prev_bucket_tips

def get_rain_fall(bucket_tips):
    """
    Calculate rain fall in mm from number of bucket tips.

    This calculation assumes a funnel with a circular opening with a diameter of
    RAIN_GAUGE_DIAMETER. The contents of a bucket just before it tips is given
    in milliliters or cm³ in RAIN_GAUGE_BUCKET_CONTENTS.
    """
    catchment_radius = float(RAIN_GAUGE_DIAMETER) / 2.0
    # Calculate the catchment area of funnel opening in cm²:
    catchment_area = math.pow(math.pi * catchment_radius, 2)
    # Calculate the rain fall, and multiply by 10 to go from cm to mm:
    return (float(RAIN_GAUGE_BUCKET_CONTENTS) / catchment_area) * 10.0

def try_read_data():
    while radio.available():
        len = radio.getDynamicPayloadSize()
        payload = radio.read(len)
        if payload == b'' or re.match(b'^(.)\\1+$', payload):
            continue

        try:
            (temp, pres, humi, buck, batt) = WeatherData.unpack(payload)
        except struct.error:
            print("Invalid data", payload)
            continue

        now = datetime.datetime.now(datetime.timezone.utc)
        data = {
            'temperature': round(temp, 2),
            'pressure': round(pres, 2),
            'humidity': round(humi, 2),
            'rain_fall': round(get_rain_fall(get_bucket_tips_today(buck)), 2),
            'battery_voltage': round(batt, 2),
            'observationTime': now.isoformat().replace('+00:00', 'Z'),
        }

        print("Weather data received, sending to web server.")
        print(data)
        apie.send_weather_data(data)

def main():
    radio.begin()
    radio.setChannel(0)
    radio.setDataRate(RF24_250KBPS)
    radio.setPALevel(RF24_PA_MAX)
    radio.setAutoAck(True)
    radio.enableDynamicPayloads()
    radio.setRetries(5, 15)
    radio.setCRCLength(RF24_CRC_16)

    radio.openWritingPipe(pipes[1])
    radio.openReadingPipe(1, pipes[0])

    radio.printDetails()

    radio.startListening()
    radio.writeAckPayload(0, b'\0')

    while True:
        try_read_data()
        time.sleep(.5)

if __name__ == '__main__':
    main()
