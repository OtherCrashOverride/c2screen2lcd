#!/bin/bash
sudo modprobe aml_i2c
sudo modprobe pwm-meson
sudo modprobe pwm-ctrl
sudo modprobe fbtft_device name=flexpfb rotate=270
sudo modprobe flexfb chip=ili9488
sudo modprobe sx865x
echo 500000 | sudo tee /sys/devices/platform/pwm-ctrl/freq0
echo 1 | sudo tee /sys/devices/platform/pwm-ctrl/enable0
echo 1023 | sudo tee /sys/devices/platform/pwm-ctrl/duty0
sudo chown root:video /dev/ge2d
sudo chmod g+rw /dev/ge2d
sudo chown root:video /dev/ion
sudo chmod g+rw /dev/ion
