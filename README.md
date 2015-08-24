Python ILI9341
==============

This project contains a python module for interfacing with ILI9341 TFT LCD display via SPI bus.

Partially based on Adafruit Arduino code

All code is GPLv2 licensed unless explicitly stated otherwise.

Building
--------

Use Black-Swift [VirtualBox VM] (http://www.black-swift.com/wiki/index.php?title=C/C%2B%2B_Building_and_Remote_Debugging_with_Eclipse)
In virtual machine change directory to /home/openwrt/openwrt
Copy sources to package/bsb_ili9341/ directory, for example

Run and say yes for new bsb_ili9341 package:
```make oldconfig```

Run for compile package:
```make *package/bsb_ili9341/compile V=s*.```

Check bin/ar71xx/packages/base/ for results (like ssd1306-i2c_0.1-1_ar71xx.ipk)

Usage
-----

First, setup SPI on your Black Swift board:

```
opkg install kmod-spi-gpio-custom
insmod spi-gpio-custom bus1=1,18,19,20,0,1000000,23
```

This mean that SCK connected to GPIO18, MOSI to GPIO19, MISO to GPIO 20 and CS to GPIO23. Also connect display pins D/C to GPIO21 and RESET to GPIO26.



Example programm

```python
from ili9341 import *

ili = ILI9341(1, 0, 21, 26)

ili.clear()

ili.pixel(10,10, 0xffff)
ili.circle_fill(200, 200, 50, 0xf00f)
ili.circle(100, 100, 50, 0xfff0)
ili.triangle(50,50,70,70,150,150,0xff00)
ili.rect(50,50,150,150,0xff00)

ili.char('1', x=1, y=1, color=0x0fff)
ili.font("ArialBlack24")
ili.write("1234567890", x=1, y=20, color=0x0fff)
```

Methods
-------

    ILI9341(bus, address, pin_dc, pin_reset)

Connects to the specified SPI bus specified pins

    clear()

Clear LCD display.

    pixel(x, y, color)

Draws pixel at specified location and color on LCD display.

    rgb2color(r, g, b)

Convert RGB to internal color.

    circle(x0, y0, radius, color)

Draws circle at specified location, radius and color on LCD display.

    circle_fill(x0, y0, radius, color)

Draws and fills circle at specified location, radius and color on LCD display.

    line(x0, y0, x1, y1, color)

Draws line at specified locations and color on LCD display.

    line_vertical(x, y, len, color)

Draws vertical line at specified location, length and color on LCD display.

    line_horisontal(x, y, len, color)

Draws horisontal line at specified location, length and color on LCD display.

    triangle(x0, y0, x1, y1, x2, y2, color)

Draws triangle at specified location and color on LCD display.

    rect(x, y, w, h, color)

Draws rect at specified location, width, height and color on LCD display.

    rect_fill(x, y, w, h, color)

Draws and fills rect at specified location, width, height and color on LCD display.

    color(c)

Set foreground color.

    bg_color(c)

Set background color.

    cursor(x, y)

Set text cursor at specified location.

    font(name, spacing=1)

Set text font name and char spacing.

    char(ch, x=0, y=0, color=1)

Draw char at current or specified position with current font and size.

    write(string, x=0, y=0, color=1)

Draw string at current or specified position with current font and size.
