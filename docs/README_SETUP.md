# nRF24L01 Weather Station Setup Guide

## 1. System Preparation
Enable SPI via `sudo raspi-config` (Interface Options > SPI) and reboot.

```bash
# Update system
sudo apt-get update && sudo apt-get upgrade -y

# Install dependencies
sudo apt-get install -y libboost-python-dev python3-dev libcurl4-openssl-dev git build-essential pigpio

# Start pigpiod daemon
sudo systemctl enable pigpiod
sudo systemctl start pigpiod

cd ~
# Move the library if it is in the wrong place
mv ~/pigpio-master/rf24libs ~ 2>/dev/null || true
cd ~/rf24libs/RF24

# Build C++ Drivers
./configure --driver=SPIDEV
make
sudo make install
sudo ldconfig

# Build Python Wrapper
cd pyRF24
sudo pip install . --break-system-packages

cd ~/WeatherStationServer

# Compilation command (Links RF24 and Curl)
g++ -Ofast "Server_TX_,mode_ACnodes.cpp" \
-I/usr/local/include/RF24 \
-L/usr/local/lib \
-lrf24 -lcurl \
-o WTEST

sudo ./WTEST
