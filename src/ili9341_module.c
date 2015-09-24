/*
 * ili9341_module.c - Python bindings for ILI9341 TFT LCD display via SPI bus
 * Copyright (C) 2015, mail@aliaksei.org
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc.
 */
#include <Python.h>
#include <structmember.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/spi/spidev.h>
#include "fonts.h"
#include "ili9341.h"

#define SYSFS_GPIO_DIR "/sys/class/gpio"

#define INPUT	0
#define OUTPUT	1

#define SPIDEV_MAXPATH	128

typedef struct {
	PyObject_HEAD
	
	int fd;	/* open file descriptor: /dev/spiX.X */	
	int fd_dc, fd_reset;
	
	int pin_reset;
	int pin_dc;
	int width;
	int height;
	int rotation;

	unsigned char *font;
	int color, bg_color, char_spacing;
	int cursor_x;
	int cursor_y;
} ILI9341PyObject;

static PyMemberDef ili9341_members[] = {
	{"cursor_x", T_INT, offsetof(ILI9341PyObject, cursor_x), 0,
		"Cursor X position"},
	{"cursor_y", T_INT, offsetof(ILI9341PyObject, cursor_y), 0,
		"Cursor Y position"},
	{NULL}  /* Sentinel */
};


static int gpioExport(int gpio);
static int gpioSetDirection(int gpio, int direction);
static int gpioOpenSet(int gpio);
static void gpioCloseSet(int gpio_fd);
static void gpioSet(int gpio_fd, int value);

#define TFT_DC_LOW		gpioSet(self->fd_dc, 0)
#define TFT_DC_HIGH		gpioSet(self->fd_dc, 1)
#define TFT_RST_LOW		gpioSet(self->fd_reset, 0)
#define TFT_RST_HIGH	gpioSet(self->fd_reset, 1)

static void TFT_sendByte(ILI9341PyObject *self, char data);
static void TFT_sendCMD(ILI9341PyObject *self, int index);
static void TFT_sendDATA(ILI9341PyObject *self, int data);
static void TFT_sendWord(ILI9341PyObject *self, int data);
static void TFT_setCol(ILI9341PyObject *self, int StartCol, int EndCol);
static void TFT_setPage(ILI9341PyObject *self, int StartPage, int EndPage);
static void TFT_setXY(ILI9341PyObject *self, int poX, int poY);
static int TFT_rgb2color(ILI9341PyObject *self, int R, int G, int B);
static void TFT_setPixel(ILI9341PyObject *self, int poX, int poY, int color);
static int TFT_char(ILI9341PyObject *self, unsigned char ch);
static int TFT_charWidth(ILI9341PyObject *self, unsigned char ch);

static void swap(int *a, int *b);

static int
ili9341_init(ILI9341PyObject *self, PyObject *args, PyObject *kwds) {
	int i;
	int bus, chip_select, pin_dc, pin_reset;
	char path[SPIDEV_MAXPATH];
	static char *kwlist[] = {"bus", "chip_select", "dc", "reset", NULL};

	if (!PyArg_ParseTupleAndKeywords(args, kwds, "iiii", kwlist, &bus, &chip_select, &pin_dc, &pin_reset))
		return -1;

	if (snprintf(path, SPIDEV_MAXPATH, "/dev/spidev%d.%d", bus, chip_select) >= SPIDEV_MAXPATH) {
		return -1;
	}

	if ((self->fd = open(path, O_RDWR)) < 0) {
		return -1;
	}

	// setup CS and RST pins
	self->pin_dc = pin_dc;
	self->pin_reset = pin_reset;

	gpioExport(self->pin_dc);
	gpioSetDirection(self->pin_dc, OUTPUT);
	self->fd_dc = gpioOpenSet(self->pin_dc);

	gpioExport(self->pin_reset);
	gpioSetDirection(self->pin_reset, OUTPUT);
	self->fd_reset = gpioOpenSet(self->pin_reset);

	self->width = ILI9341_TFTWIDTH;
	self->height = ILI9341_TFTHEIGHT;
	self->color = 0xffff;
	self->bg_color = 0;
	self->cursor_x = 0;
	self->cursor_y = 0;

	self->font = System5x7;
	self->char_spacing = 1;

	TFT_DC_HIGH;

	TFT_RST_LOW;
	for(i=0; i<0x7FFFFF; i++);
	TFT_RST_HIGH;

	TFT_sendCMD(self, 0xEF);
	TFT_sendDATA(self, 0x03);
	TFT_sendDATA(self, 0x80);
	TFT_sendDATA(self, 0x02);
  
	TFT_sendCMD(self, 0xCB);
	TFT_sendDATA(self, 0x39);
	TFT_sendDATA(self, 0x2C);
	TFT_sendDATA(self, 0x00);
	TFT_sendDATA(self, 0x34);
	TFT_sendDATA(self, 0x02);
  
	TFT_sendCMD(self, 0xCF);
	TFT_sendDATA(self, 0x00);
	TFT_sendDATA(self, 0xC1);
	TFT_sendDATA(self, 0x30);

	TFT_sendCMD(self, 0xE8);
	TFT_sendDATA(self, 0x85);
	TFT_sendDATA(self, 0x00);
	TFT_sendDATA(self, 0x78);

	TFT_sendCMD(self, 0xEA);
	TFT_sendDATA(self, 0x00);
	TFT_sendDATA(self, 0x00);

	TFT_sendCMD(self, 0xED);
	TFT_sendDATA(self, 0x64);
	TFT_sendDATA(self, 0x03);
	TFT_sendDATA(self, 0x12);
	TFT_sendDATA(self, 0x81);

	TFT_sendCMD(self, 0xF7);
	TFT_sendDATA(self, 0x20);

	TFT_sendCMD(self, ILI9341_PWCTR1);    	//Power control
	TFT_sendDATA(self, 0x23);   	//VRH[5:0]

	TFT_sendCMD(self, ILI9341_PWCTR2);    	//Power control
	TFT_sendDATA(self, 0x10);   	//SAP[2:0];BT[3:0]

	TFT_sendCMD(self, ILI9341_VMCTR1);    	//VCM control
	TFT_sendDATA(self, 0x3e);   	//Contrast
	TFT_sendDATA(self, 0x28);

	TFT_sendCMD(self, ILI9341_VMCTR2);    	//VCM control2
	TFT_sendDATA(self, 0x86);  	 //--

	TFT_sendCMD(self, ILI9341_MADCTL);    	// Memory Access Control
	TFT_sendDATA(self, MADCTL_MX | MADCTL_BGR);

	TFT_sendCMD(self, ILI9341_PIXFMT);
	TFT_sendDATA(self, 0x55);

	TFT_sendCMD(self, ILI9341_FRMCTR1);
	TFT_sendDATA(self, 0x00);
	TFT_sendDATA(self, 0x18);

	TFT_sendCMD(self, ILI9341_DFUNCTR);    	// Display Function Control
	TFT_sendDATA(self, 0x08);
	TFT_sendDATA(self, 0x82);
	TFT_sendDATA(self, 0x27);

	TFT_sendCMD(self, 0xF2);    	// 3Gamma Function Disable
	TFT_sendDATA(self, 0x00);

	TFT_sendCMD(self, ILI9341_GAMMASET);    	//Gamma curve selected
	TFT_sendDATA(self, 0x01);

	TFT_sendCMD(self, ILI9341_GMCTRP1);    	//Set Gamma
	TFT_sendDATA(self, 0x0F);
	TFT_sendDATA(self, 0x31);
	TFT_sendDATA(self, 0x2B);
	TFT_sendDATA(self, 0x0C);
	TFT_sendDATA(self, 0x0E);
	TFT_sendDATA(self, 0x08);
	TFT_sendDATA(self, 0x4E);
	TFT_sendDATA(self, 0xF1);
	TFT_sendDATA(self, 0x37);
	TFT_sendDATA(self, 0x07);
	TFT_sendDATA(self, 0x10);
	TFT_sendDATA(self, 0x03);
	TFT_sendDATA(self, 0x0E);
	TFT_sendDATA(self, 0x09);
	TFT_sendDATA(self, 0x00);

	TFT_sendCMD(self, ILI9341_GMCTRN1);    	//Set Gamma
	TFT_sendDATA(self, 0x00);
	TFT_sendDATA(self, 0x0E);
	TFT_sendDATA(self, 0x14);
	TFT_sendDATA(self, 0x03);
	TFT_sendDATA(self, 0x11);
	TFT_sendDATA(self, 0x07);
	TFT_sendDATA(self, 0x31);
	TFT_sendDATA(self, 0xC1);
	TFT_sendDATA(self, 0x48);
	TFT_sendDATA(self, 0x08);
	TFT_sendDATA(self, 0x0F);
	TFT_sendDATA(self, 0x0C);
	TFT_sendDATA(self, 0x31);
	TFT_sendDATA(self, 0x36);
	TFT_sendDATA(self, 0x0F);

	TFT_sendCMD(self, ILI9341_SLPOUT);    	//Exit Sleep
	for(i=0; i<0x7FFFFF; i++);

	TFT_sendCMD(self, ILI9341_DISPON);    //Display on
	TFT_sendCMD(self, 0x2c);

	return 0;
}

static PyObject *
ili9341_clear(ILI9341PyObject *self, PyObject *unused) {
	unsigned char *sendBuffer;
	unsigned char *receiveBuffer;
    struct spi_ioc_transfer xfer;
	int i, bytes = (ILI9341_TFTWIDTH * ILI9341_TFTHEIGHT);

	TFT_setCol(self, 0, self->width);
	TFT_setPage(self, 0, self->height);
	TFT_sendCMD(self, 0x2c);	// start to write to display ram

	TFT_DC_HIGH;

	for(i=0; i<bytes; i++) {
		TFT_sendWord(self, 0);
	}

	Py_RETURN_NONE;
}

static PyObject *
ili9341_rotation(ILI9341PyObject *self, PyObject *args) {
	int mode;
	
	if (!PyArg_ParseTuple(args, "i", &mode)) {
		return NULL;
	}

	self->rotation = mode % 4;

	TFT_sendCMD(self, ILI9341_MADCTL);

	switch (self->rotation) {
		case 0:
			TFT_sendCMD(self, MADCTL_MX | MADCTL_BGR);
			self->width  = ILI9341_TFTWIDTH;
			self->height = ILI9341_TFTHEIGHT;
			break;
		case 1:
			TFT_sendCMD(self, MADCTL_MV | MADCTL_BGR);
			self->width  = ILI9341_TFTHEIGHT;
			self->height = ILI9341_TFTWIDTH;
			break;
		case 2:
			TFT_sendCMD(self, MADCTL_MY | MADCTL_BGR);
			self->width  = ILI9341_TFTWIDTH;
			self->height = ILI9341_TFTHEIGHT;
			break;
		case 3:
			TFT_sendCMD(self, MADCTL_MX | MADCTL_MY | MADCTL_MV | MADCTL_BGR);
			self->width  = ILI9341_TFTHEIGHT;
			self->height = ILI9341_TFTWIDTH;
			break;
	}

	Py_RETURN_NONE;
}

static PyObject *
ili9341_invert(ILI9341PyObject *self, PyObject *args) {
	int mode;
	
	if (!PyArg_ParseTuple(args, "i", &mode)) {
		return NULL;
	}

	TFT_sendCMD(self, mode ? ILI9341_INVON : ILI9341_INVOFF);

	Py_RETURN_NONE;
}

static PyObject *
ili9341_rgb2color(ILI9341PyObject *self, PyObject *args) {
	int R, G, B;
	int rgb;
    PyObject *pValue;
	
	if (!PyArg_ParseTuple(args, "iii", &R, &G, &B)) {
		return NULL;
	}

	rgb = TFT_rgb2color(self, R, G, B);
	
	return Py_BuildValue("i", rgb);
}

static PyObject *
ili9341_drawPixel(ILI9341PyObject *self, PyObject *args) {
	int x, y, color;

	if (!PyArg_ParseTuple(args, "iii", &x, &y, &color)) {
		return NULL;
	}

	TFT_setPixel(self, x, y, color);

	Py_RETURN_NONE;
}

// Bresenham's algorithm - thx wikpedia
static PyObject *
ili9341_drawLine(ILI9341PyObject *self, PyObject *args) {
	int x0, y0, x1, y1, color;
	
	if (!PyArg_ParseTuple(args, "iiiii", &x0, &y0, &x1, &y1, &color)) {
		return NULL;
	}
	
	int16_t steep = abs(y1 - y0) > abs(x1 - x0);

	if (steep) {
		swap(&x0, &y0);
		swap(&x1, &y1);
	}

	if (x0 > x1) {
		swap(&x0, &x1);
		swap(&y0, &y1);
	}

	int16_t dx, dy;
	dx = x1 - x0;
	dy = abs(y1 - y0);

	int16_t err = dx / 2;
	int16_t ystep;

	if (y0 < y1) {
		ystep = 1;
	} else {
		ystep = -1;
	}

	for (; x0<=x1; x0++) {
		if (steep) {
			TFT_setPixel(self, y0, x0, color);
		} else {
			TFT_setPixel(self, x0, y0, color);
		}
		err -= dy;
		if (err < 0) {
			y0 += ystep;
			err += dx;
		}
	}

	Py_RETURN_NONE;
}

static PyObject *
ili9341_drawFastVLine(ILI9341PyObject *self, PyObject *args) {
	int x, y, len, color;
    PyObject *pArgs;
	
	if (!PyArg_ParseTuple(args, "iiii", &x, &y, &len, &color)) {
		return NULL;
	}

	pArgs = Py_BuildValue("iiiii", x, y, x, y+len-1, color);
	ili9341_drawLine(self, pArgs);
	
	Py_RETURN_NONE;
}

static PyObject *
ili9341_drawFastHLine(ILI9341PyObject *self, PyObject *args) {
	int x, y, len, color;
    PyObject *pArgs;
	
	if (!PyArg_ParseTuple(args, "iiii", &x, &y, &len, &color)) {
		return NULL;
	}

	pArgs = Py_BuildValue("iiiii", x, y, x+len-1, y, color);
	ili9341_drawLine(self, pArgs);
	
	Py_RETURN_NONE;
}

static PyObject *
ili9341_drawTriangle(ILI9341PyObject *self, PyObject *args) {
	int i;
	int x0, y0, x1, y1, x2, y2, color;
    PyObject *pArgs;

	if (!PyArg_ParseTuple(args, "iiiiiii", &x0, &y0, &x1, &y1, &x2, &y2, &color)) {
		return NULL;
	}

	pArgs = Py_BuildValue("iiiii", x0, y0, x1, y1, color);
	ili9341_drawLine(self, pArgs);
	pArgs = Py_BuildValue("iiiii", x1, y1, x2, y2, color);
	ili9341_drawLine(self, pArgs);
	pArgs = Py_BuildValue("iiiii", x0, y0, x2, y2, color);
	ili9341_drawLine(self, pArgs);

	Py_RETURN_NONE;
}


static PyObject *
ili9341_drawRect(ILI9341PyObject *self, PyObject *args) {
	int i;
	int x, y, w, h, color;
    PyObject *pArgs;

	if (!PyArg_ParseTuple(args, "iiiii", &x, &y, &w, &h, &color)) {
		return NULL;
	}

	pArgs = Py_BuildValue("iiii", x, y, w, color);
	ili9341_drawFastHLine(self, pArgs);

	pArgs = Py_BuildValue("iiii", x, y+h-1, w, color);
	ili9341_drawFastHLine(self, pArgs);

	pArgs = Py_BuildValue("iiii", x, y, h, color);
	ili9341_drawFastVLine(self, pArgs);

	pArgs = Py_BuildValue("iiii", x+w-1, y, h, color);
	ili9341_drawFastVLine(self, pArgs);
	
	Py_RETURN_NONE;
}

static PyObject *
ili9341_fillRect(ILI9341PyObject *self, PyObject *args) {
	int i;
	int x, y, w, h, color;
    PyObject *pArgs;

	if (!PyArg_ParseTuple(args, "iiiii", &x, &y, &w, &h, &color)) {
		return NULL;
	}

	// Update in subclasses if desired!
	for (i=x; i<x+w; i++) {
		pArgs = Py_BuildValue("iiii", i, y, h, color);
		ili9341_drawFastVLine(self, pArgs);
	}

	Py_RETURN_NONE;
}

static PyObject *
ili9341_drawCircle(ILI9341PyObject *self, PyObject *args) {
	int x0, y0, r, color;

	if (!PyArg_ParseTuple(args, "iiii", &x0, &y0, &r, &color)) {
		return NULL;
	}
	
	int16_t f = 1 - r;
	int16_t ddF_x = 1;
	int16_t ddF_y = -2 * r;
	int16_t x = 0;
	int16_t y = r;

	TFT_setPixel(self, x0, y0+r, color);
	TFT_setPixel(self, x0, y0-r, color);
	TFT_setPixel(self, x0+r, y0, color);
	TFT_setPixel(self, x0-r, y0, color);

	while (x < y) {
		if (f >= 0) {
			y--;
			ddF_y += 2;
			f += ddF_y;
		}
		x++;
		ddF_x += 2;
		f += ddF_x;

		TFT_setPixel(self, x0 + x, y0 + y, color);
		TFT_setPixel(self, x0 - x, y0 + y, color);
		TFT_setPixel(self, x0 + x, y0 - y, color);
		TFT_setPixel(self, x0 - x, y0 - y, color);
		TFT_setPixel(self, x0 + y, y0 + x, color);
		TFT_setPixel(self, x0 - y, y0 + x, color);
		TFT_setPixel(self, x0 + y, y0 - x, color);
		TFT_setPixel(self, x0 - y, y0 - x, color);
	}

	Py_RETURN_NONE;
}


static PyObject *
ili9341_fillCircle(ILI9341PyObject *self, PyObject *args) {
	int poX, poY, r, color;
    PyObject *pArgs;

	if (!PyArg_ParseTuple(args, "iiii", &poX, &poY, &r, &color)) {
		return NULL;
	}

    int x = -r, y = 0, err = 2-2*r, e2;

    do {
		pArgs = Py_BuildValue("iiii", poX-x, poY-y, 2*y, color);
		ili9341_drawFastVLine(self, pArgs);

		pArgs = Py_BuildValue("iiii", poX+x, poY-y, 2*y, color);
		ili9341_drawFastVLine(self, pArgs);

        e2 = err;
        if (e2 <= y) {
            err += ++y*2+1;
            if (-x == y && e2 <= x) e2 = 0;
        }
        if (e2 > x) err += ++x*2+1;
    } while (x <= 0);

	Py_RETURN_NONE;
}

static PyObject *
ili9341_setCursor(ILI9341PyObject *self, PyObject *args) {
	int x, y;
	
	if (!PyArg_ParseTuple(args, "ii", &x, &y)) {
		return NULL;
	}

	if (x <= self->width)
		self->cursor_x = x;

	if (y <= self->height)
		self->cursor_y = y;

	Py_RETURN_NONE;
}


static PyObject *
ili9341_setColor(ILI9341PyObject *self, PyObject *args) {
	int color;
	
	if (!PyArg_ParseTuple(args, "i", &color)) {
		return NULL;
	}

	self->color = color;

	Py_RETURN_NONE;
}

static PyObject *
ili9341_setBgColor(ILI9341PyObject *self, PyObject *args) {
	int color;
	
	if (!PyArg_ParseTuple(args, "i", &color)) {
		return NULL;
	}

	self->bg_color = color;

	Py_RETURN_NONE;
}

static PyObject *
ili9341_setFont(ILI9341PyObject *self, PyObject *args, PyObject *kwds) {
	int i, spacing = 1;
	unsigned char *font;
	font_info *f = fonts_table;
	static char *kwlist[] = {"font", "spacing", NULL};

	if (!PyArg_ParseTupleAndKeywords(args, kwds, "s|i",  kwlist, &font, &spacing)) {
		return NULL;
	}
	
	self->char_spacing = spacing;

	while (f->name != NULL) {
		if (strcmp(f->name, font) == 0) {
			self->font = f->data;
			break;
		}
		f++;
	}

	Py_RETURN_NONE;
}

static PyObject *
ili9341_drawChar(ILI9341PyObject *self, PyObject *args, PyObject *kwds) {
	int x = self->cursor_x, y = self->cursor_y, size = 1;
	int color = 1, bg = 0;
	unsigned char ch;
	static char *kwlist[] = {"ch", "x", "y", "color", NULL};

	if (!PyArg_ParseTupleAndKeywords(args, kwds, "c|iii", kwlist, &ch, &x, &y, &color)) {
		return NULL;
	}
	
	self->cursor_x = x;
	self->cursor_y = y;
	self->color = color;

	TFT_char(self, ch);
	
	Py_RETURN_NONE;
}

static PyObject *
ili9341_writeString(ILI9341PyObject *self, PyObject *args, PyObject *kwds) {
	int i, w;
	unsigned char *str, ch;
	int x = self->cursor_x, y = self->cursor_y, color = self->color;
	static char *kwlist[] = {"str", "x", "y", "color", NULL};
	unsigned char *font = self->font;
	
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "s|iii", kwlist, &str, &x, &y, &color)) {
		return NULL;
	}
	
	self->cursor_x = x;
	self->cursor_y = y;
	self->color = color;

	for(i=0; i<strlen(str); i++) {
		ch = str[i];
		w = TFT_charWidth(self, ch) + self->char_spacing;
		TFT_char(self, ch);
		
		if ((self->cursor_x + w) <= self->width) {
			self->cursor_x += w;
		}
		else if ((self->cursor_y + font[FONT_HEIGHT] + self->char_spacing) <= self->height) {
			self->cursor_x = 0;
			self->cursor_y += font[FONT_HEIGHT] + self->char_spacing;
		}
	}

	Py_RETURN_NONE;
}


static
int gpioExport(int gpio) {
	int fd, len;
	char buf[255];

	fd = open(SYSFS_GPIO_DIR "/export", O_WRONLY);
	if (fd) {
		len = snprintf(buf, sizeof(buf), "%d", gpio);
		write(fd, buf, len);
		close(fd);
	}
}

static
int gpioSetDirection(int gpio, int direction) {
	int fd, len;
	char buf[255];

	snprintf(buf, sizeof(buf), SYSFS_GPIO_DIR "/gpio%d/direction", gpio);
    fd = open(buf, O_WRONLY);
	
	if (fd) {
		if (direction) {
			write(fd, "out", 3);
		}
		else {
			write(fd, "in", 2);
		}
		close(fd);
	}
}

static
int gpioOpenSet(int gpio) {
	char buf[255];

	snprintf(buf, sizeof(buf), SYSFS_GPIO_DIR "/gpio%d/value", gpio);

	return open(buf, O_WRONLY);
}

static
void gpioCloseSet(int gpio_fd) {
	if (gpio_fd) {
		close(gpio_fd);
	}
}

static
void gpioSet(int gpio_fd, int value) {
	if (gpio_fd) {
		if (value) {
			write(gpio_fd, "1", 2);
		}
		else {
			write(gpio_fd, "0", 2);
		}
	}
}

static
void TFT_sendByte(ILI9341PyObject *self, char data) {
    struct spi_ioc_transfer xfer;
   
	memset(&xfer, 0, sizeof(xfer));
    xfer.tx_buf = (unsigned long)&data;
    xfer.len = sizeof(char);

    ioctl(self->fd, SPI_IOC_MESSAGE(1), &xfer);
}

static
void TFT_sendCMD(ILI9341PyObject *self, int index) {
    TFT_DC_LOW;
    TFT_sendByte(self, index);
}

static
void TFT_sendDATA(ILI9341PyObject *self, int data) {
    TFT_DC_HIGH;
    TFT_sendByte(self, data);
}

static
void TFT_sendWord(ILI9341PyObject *self, int data) {
    TFT_DC_HIGH;

    TFT_sendByte(self, data >> 8);
    TFT_sendByte(self, data & 0x00ff);
}

static
void TFT_setCol(ILI9341PyObject *self, int StartCol, int EndCol) {
	TFT_sendCMD(self, 0x2A);	// Column Command address
	TFT_sendWord(self, StartCol);
	TFT_sendWord(self, EndCol);
}

static
void TFT_setPage(ILI9341PyObject *self, int StartPage, int EndPage) {
	TFT_sendCMD(self, 0x2B);	// Column Command address
	TFT_sendWord(self, StartPage);
	TFT_sendWord(self, EndPage);
}

static
void TFT_setXY(ILI9341PyObject *self, int poX, int poY) {
	TFT_setCol(self, poX, poX);
	TFT_setPage(self, poY, poY);
	TFT_sendCMD(self, 0x2c);
}

static
int TFT_rgb2color(ILI9341PyObject *self, int R, int G, int B) {
	int rgb;

	rgb = R & 0x1F;
	rgb <<= 6;
	rgb |= G & 0x3F;
	rgb <<= 5;
	rgb |= B & 0x1F;
	
	// rgb = ((R & 0xF8) << 8) | ((G & 0xFC) << 3) | (B >> 3);

	return rgb;
}

static
void TFT_setPixel(ILI9341PyObject *self, int poX, int poY, int color) {
	TFT_setXY(self, poX, poY);
	TFT_sendWord(self, color);
}

static
int TFT_char(ILI9341PyObject *self, unsigned char ch) {
	int bX = self->cursor_x, bY = self->cursor_y, fgcolour = self->color, bgcolour = self->bg_color;
	int i, j, k;
	char c = ch;
	unsigned char *font = self->font;
	uint8_t width = 0;
	uint8_t height = font[FONT_HEIGHT];
	uint8_t bytes = (height + 7) / 8;
	uint8_t firstChar = font[FONT_FIRST_CHAR];
	uint8_t charCount = font[FONT_CHAR_COUNT];
	uint16_t index = 0;

	if (bX >= self->width || bY >= self->height) return -1;

	if (c == ' ') {
		width = TFT_charWidth(self, ' ');
		PyObject *pArgs = Py_BuildValue("iiiii", bX, bY, bX + width, bY + height, bgcolour);
		ili9341_fillRect(self, pArgs);

		return width;
	}

	if (c < firstChar || c >= (firstChar + charCount)) return 0;
	c -= firstChar;

	if (font[FONT_LENGTH] == 0 && font[FONT_LENGTH + 1] == 0) {
		// zero length is flag indicating fixed width font (array does not contain width data entries)
		width = font[FONT_FIXED_WIDTH];
		index = c * bytes * width + FONT_WIDTH_TABLE;
	} else {
		// variable width font, read width data, to get the index
		for (i = 0; i < c; i++) {
			index += font[FONT_WIDTH_TABLE + i];
		}
		index = index * bytes + charCount + FONT_WIDTH_TABLE;
		width = font[FONT_WIDTH_TABLE + c];
	}

	if (bX < -width || bY < -height) return width;

	// last but not least, draw the character
	for (j = 0; j < width; j++) { // Width
		// for (i = bytes - 1; i < 254; i--) { // Vertical Bytes
		for (i = 0; i < bytes; i++) { // Vertical Bytes
			uint8_t data = font[index + j + (i * width)];
			int offset = (i * 8);

			if ((i == bytes - 1) && bytes > 1) {
				offset = height - 8;
			} else if (height<8) {
				offset = height - 7;
			}

			for (k = 0; k < 8; k++) { // Vertical bits
				if ((offset+k >= i*8) && (offset+k <= height)) {
					if (data & (1 << k)) {
						TFT_setPixel(self, bX + j, bY + offset + k, fgcolour);
					} else {
						TFT_setPixel(self, bX + j, bY + offset + k, bgcolour);
					}
				}
			}
		}
	}

	return width;
}

static
int TFT_charWidth(ILI9341PyObject *self, unsigned char ch) {
    char c = ch;

    // Space is often not included in font so use width of 'n'
    if (c == ' ') c = 'n';
    uint8_t width = 0;

    uint8_t firstChar = self->font[FONT_FIRST_CHAR];
    uint8_t charCount = self->font[FONT_FIRST_CHAR];

    uint16_t index = 0;

    if (c < firstChar || c >= (firstChar + charCount)) {
	    return 0;
    }
    c -= firstChar;

	if (self->font[FONT_LENGTH] == 0 && self->font[FONT_LENGTH + 1] == 0) {
	    // zero length is flag indicating fixed width font (array does not contain width data entries)
	    width = self->font[FONT_FIXED_WIDTH];
    } else {
	    // variable width font, read width data
		width = self->font[FONT_WIDTH_TABLE + c];
    }

    return width;
}

static
void swap(int *a, int *b) {
	int temp;

	temp = *b;
	*b   = *a;
	*a   = temp;   
}


static PyMethodDef ili9341_methods[] = {
	{"clear", (PyCFunction)ili9341_clear, METH_NOARGS,
		"clear()\n\n Clear LCD display."},
	{"rotation", (PyCFunction)ili9341_rotation, METH_VARARGS,
		"rotation(mode)\n\n Set rotation mode (0-3)."},
	{"invert", (PyCFunction)ili9341_invert, METH_VARARGS,
		"invert(mode)\n\n Invert LCD display."},
	{"rgb2color", (PyCFunction)ili9341_rgb2color, METH_VARARGS,
		"rgb2color(r, g, b)\n\n Convert RGB to internal color."},
	{"pixel", (PyCFunction)ili9341_drawPixel, METH_VARARGS,
		"pixel(x, y, color)\n\n Draws pixel at specified location and color on LCD display."},
	{"circle", (PyCFunction)ili9341_drawCircle, METH_VARARGS,
		"circle(x, y, radius, color)\n\n Draws circle at specified location, radius and color on LCD display."},
	{"circle_fill", (PyCFunction)ili9341_fillCircle, METH_VARARGS,
		"circle_fill(x, y, radius, color)\n\n Draws and fills circle at specified location, radius and color on LCD display."},
	{"line", (PyCFunction)ili9341_drawLine, METH_VARARGS,
		"line(x0, y0, x1, y1, color)\n\n Draws line at specified locations and color on LCD display."},
	{"line_vertical", (PyCFunction)ili9341_drawFastVLine, METH_VARARGS,
		"line_vertical(x, y, len, color)\n\n Draws vertical line at specified location, length and color on LCD display."},
	{"line_horisontal", (PyCFunction)ili9341_drawFastHLine, METH_VARARGS,
		"line_horisontal(x, y, len, color)\n\n Draws horisontal line at specified location, length and color on LCD display."},
	{"triangle", (PyCFunction)ili9341_drawTriangle, METH_VARARGS,
		"triangle(x0, y0, x1, y1, x2, y2, color)\n\n Draws triangle at specified location and color on LCD display."},
	{"rect", (PyCFunction)ili9341_drawRect, METH_VARARGS,
		"rect(x, y, w, h, color)\n\n Draws rect at specified location, width, height and color on LCD display."},
	{"rect_fill", (PyCFunction)ili9341_fillRect, METH_VARARGS,
		"rect_fill(x, y, w, h, color)\n\n Draws and fills rect at specified location, width, height and color on LCD display."},
	{"color", (PyCFunction)ili9341_setColor, METH_VARARGS,
		"bg_color(c)\n\n Set foreground color."},
	{"bg_color", (PyCFunction)ili9341_setBgColor, METH_VARARGS,
		"bg_color(c)\n\n Set background color."},
	{"cursor", (PyCFunction)ili9341_setCursor, METH_VARARGS,
		"cursor(x, y)\n\n Set text cursor at specified location."},
	{"font", (PyCFunction)ili9341_setFont, METH_VARARGS | METH_KEYWORDS,
		"font(name, spacing=1)\n\n Set text font name and char spacing."},
	{"char", (PyCFunction)ili9341_drawChar, METH_VARARGS | METH_KEYWORDS,
		"char(ch, x=0, y=0, color=1)\n\n Draw char at current or specified position with current font and size."},
	{"write", (PyCFunction)ili9341_writeString, METH_VARARGS | METH_KEYWORDS,
		"write(string, x=0, y=0, color=1)\n\n Draw string at current or specified position with current font and size."},
	{NULL}
};

static PyTypeObject ILI9341ObjectType = {
	PyObject_HEAD_INIT(NULL)
	0,				/* ob_size        */
	"ILI9341",		/* tp_name        */
	sizeof(ILI9341PyObject),		/* tp_basicsize   */
	0,				/* tp_itemsize    */
	0,				/* tp_dealloc     */
	0,				/* tp_print       */
	0,				/* tp_getattr     */
	0,				/* tp_setattr     */
	0,				/* tp_compare     */
	0,				/* tp_repr        */
	0,				/* tp_as_number   */
	0,				/* tp_as_sequence */
	0,				/* tp_as_mapping  */
	0,				/* tp_hash        */
	0,				/* tp_call        */
	0,				/* tp_str         */
	0,				/* tp_getattro    */
	0,				/* tp_setattro    */
	0,				/* tp_as_buffer   */
	Py_TPFLAGS_DEFAULT,		/* tp_flags       */
	"ILI9341(bus, chip_select, pin_dc, pin_reset) -> LCD\n\nReturn a new ILI9341 object that is connected to the specified bus and pins.\n",	/* tp_doc         */
	0,				/* tp_traverse       */
	0,				/* tp_clear          */
	0,				/* tp_richcompare    */
	0,				/* tp_weaklistoffset */
	0,				/* tp_iter           */
	0,				/* tp_iternext       */
	ili9341_methods,	/* tp_methods        */
	ili9341_members,	/* tp_members        */
	0,				/* tp_getset         */
	0,				/* tp_base           */
	0,				/* tp_dict           */
	0,				/* tp_descr_get      */
	0,				/* tp_descr_set      */
	0,				/* tp_dictoffset     */
	(initproc)ili9341_init,		/* tp_init           */
};

PyMODINIT_FUNC
initili9341(void) 
{
	PyObject* m;

	ILI9341ObjectType.tp_new = PyType_GenericNew;
	if (PyType_Ready(&ILI9341ObjectType) < 0)
		return;

	m = Py_InitModule3("ili9341", NULL,
		   "Python bindings for ILI9341 TFT LCD display via SPI bus");
	if (m == NULL)
		return;

	Py_INCREF(&ILI9341ObjectType);
	PyModule_AddObject(m, "ILI9341", (PyObject *)&ILI9341ObjectType);
}
