# omega2-ws2811-lkm
Linux kernel module for Onion Omega2 to control WS2811/WS2812 LEDs.

It's using a bit-banging, so you can use any GPIO pin. Also, it supports multiple pins simultaneously and writing them at the same time, so you can connect multiple LED chains to different pins and increase FPS.

## How to load
Put **ws2811.ko** into /lib/modules/*kernel_version*/ directory

There are two parameters:
* **pins** - array of pin numbers
* **led_count** - LED count per pin

For example you are using pins 11,15,16,17 and 300 LEDs per each pin, command to load module:

    insmod ws2811 pins=11,15,16,17 led_count=300

or create /etc/modules.d/ws2811 file and put this string there:

    ws2811 pins=11,15,16,17 led_count=300

to load the module automatically at the system boot. Don't forget to set up GPIO muxing if need.

## How to use
If the module is loaded successfully **/dev/ws2811** pseudo-file should appear. You can read and write it - three bytes per RGB LED.
Synchronization appears after each writes operation, so it is desirable to write all bytes at once.
