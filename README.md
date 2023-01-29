# temp-sensor-esp8266
Simple temperature/humidity sensor based on SHT31 with MQTT integration and IotWebConf support.


## Features

* Reads temperature and humidity from SHT31 sensor
* Connect to MQTT server and send temperature and humidity to MQTT
* Web config interface for WIFI and MQTT connection data


## Environment

* Tested with ESP8266 on Wemos D1 mini
* Tested with SolarEdge SE7K inverter with BYD battery box
* Tested with Modbus over TCP

## Libraries

This project uses the following libraries. Thanks to all creators and contributors.

* OneButton
* IotWebConf
* [256dpi/arduino-mqtt](https://github.com/256dpi/arduino-mqtt)
* several Adafruit libraries


## Building

This library was created using [PlatformIO](https://platformio.org/) for [Visual Studio Code](https://code.visualstudio.com/).

To build the project with PlatformIO use the following steps:

* clone the repository from GitHub to your workspace
* compile and upload


## Last Changes

* 01/29/2023 Fhanged from BME280 to SHT31 sensor as BME280 is using temp values for correcting humidity readings. BME280 temp values are only "estimates" according to the sensors specs.
* 01/07/2023 First release no changes yet.

## License

The code in this repo is licensed under the MIT License. See LICENSE.txt for more info.