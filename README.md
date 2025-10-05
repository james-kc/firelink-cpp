# scarecrow
Repo containing all code required for the avionics and analysis of the data from the banshee rocket.

## Useful commands:

`sudo apt install -y i2c-tools`

`i2cdetect -y 1`

`sudo apt install gpiod`

`gpioset gpiochip0 4=0`

## Compilation:

```
g++ -Wall -Wextra -std=c++17 -Iinclude src/main.cpp src/sensors/*.cpp -o firelink -lm
```