#!/bin/bash

#v4l2-ctl -c brightness=0
#v4l2-ctl -c contrast=120
#v4l2-ctl -c white_balance_temperature_auto=0
#v4l2-ctl -c gamma=120
#v4l2-ctl -c white_balance_temperature=4700
#v4l2-ctl -c sharpness=100
#v4l2-ctl -c backlight_compensation=0
#v4l2-ctl -c focus_absolute=10
#v4l2-ctl --list-ctrls-menus
#v4l2-ctl -c focus_auto=0

# Set exposure_auto (menu): min=0 max=3 default=1
# 1: Manual Mode
# 3: Aperture Priority Mode
v4l2-ctl -c exposure_auto=1 -d /dev/video0
v4l2-ctl -c exposure_auto=1 -d /dev/video1  



