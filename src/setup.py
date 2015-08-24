#!/usr/bin/env python

from distutils.core import setup, Extension

classifiers = ['Development Status :: 5 - Production/Stable',
               'Operating System :: POSIX :: Linux',
               'License :: OSI Approved :: GNU General Public License v2 (GPLv2)',
               'Intended Audience :: Developers',
               'Programming Language :: Python :: 2.7',
               'Topic :: Software Development',
               'Topic :: System :: Hardware',
               'Topic :: System :: Hardware :: Hardware Drivers']

setup(	name	= "ili9341",
	version		= "0.1",
	description	= "Python bindings for ILI9341 TFT LCD display via SPI bus",
	# long_description = open('README.md').read() + "\n" + open('CHANGELOG.md').read(),
	author		= "Aliaksei",
	author_email	= "mail@aliaksei.org",
	maintainer	= "Aliaksei",
	maintainer_email= "mail@aliaksei.org",
	license		= "GPLv2",
	classifiers	= classifiers,
	url		= "https://github.com/polkabana/bsb_ili9341",
	ext_modules	= [Extension("ili9341", ["ili9341_module.c"])]
)
