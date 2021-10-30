#!/bin/bash

sudo rmmod usbhid
sudo modprobe usbhid quirks=0x20A0:0x41E5:0x0004
sudo insmod blinkdrv.ko
