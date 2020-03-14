#!/bin/bash

g++ -I /usr/include/ -o multigrab v4l2-multigrab.cpp -L /usr/lib/aarch64-linux-gnu -lv4l2 -ljpeg -fpermissive
