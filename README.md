Happy Plants
============

With a modern, wifi equipped microcontroller, like the ESP32, it becomes very easy to monitor
the environment of plant grow systems. The easiest thing to do is the readout of a temperature
and humidity sensor, like the DHT22. Of cause this project uses these sensor as well.

It is a bit more complicated to switch the light on and off. The same is valid for a water pump.
With the happy plant firmware, it is possible to trigger the switches via two separated relays.
The timing intervals are easy to setup via the web interface, which looks like this:

![screenshot](https://repository-images.githubusercontent.com/336855480/328fa980-6a17-11eb-9929-4065c126b976)

The setup:

Happy plants use a ESP32 Devkit 1 module. A DHT22 and a module with two relays are attached to the ESP32.
The ESPAsyncWebServer with the underlying AsyncTCP provide the web server environment. Some files are
stored on the ESP32 SPIFFS. The essential files are in the static/ folder in the repository (happyPlant.css
and happyPlant.js). Some other files get created during runtime. The updates of the current sensors and
switch states are streamed to the clients via websocket.

Other sensors are planed to get plugged in, like a EC/TDS sensor, a pH sensor. Maybe a fan control.

The project isn't yet ready but in production.

Have phun.
