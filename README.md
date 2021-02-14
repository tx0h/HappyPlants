HappyPlants
============

With a modern, wifi equipped microcontroller, like the ESP32, it becomes very easy to monitor
the environment of plant grow systems. The easiest thing to do is the readout of a temperature
and humidity sensor, like the DHT22. Of cause this project uses these sensor as well.

It is a bit more complicated to switch the light on and off. The same is valid for a water pump.
With the happy plant firmware, it is possible to trigger the switches via two separated relays.
The timing intervals are easy to setup via the web interface, which looks like this:

![screenshot](https://repository-images.githubusercontent.com/336855480/df14d400-6ecc-11eb-9467-27afc8dc7374)

The setup:

*HappyPlants* use a ESP32 Devkit 1 module. A DHT22 and a module with two relays
are attached to the ESP32. The ESPAsyncWebServer with the underlying AsyncTCP
provide the web server environment. Some files are stored on the ESP32 SPIFFS.
The essential files are in the static/ folder in the repository (_happyPlant.css_
and _happyPlant.js_). Some other files get created during runtime. The updates of
the current sensors and switch states are streamed to the clients via websocket.

Other sensors are planed to get plugged in, like a EC/TDS sensor, a pH sensor.
Maybe a fan control.

The project isn't yet ready but in production.

You need to store happyPlants.js and happyPlants.css on the ESP32 SPIFFS.
The webserver is able to accept uploads. You can use a tool like **_curl_** to
upload the files, like so:
```
curl -F 'file=@/PATH_TO/static/happyPlants.js' http://IP_ADDR_OF_THE_ESP32/upload
curl -F 'file=@/PATH_TO/static/happyPlants.css' http://IP_ADDR_OF_THE_ESP32/upload
```

thanks for your engagement & have phun.
