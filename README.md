Toolchain Setup (macOS Apple Silicon)
# Install dependencies
```
xcode-select --install
brew install cmake python3
brew install --cask gcc-arm-embedded
```

# Clone the Pico SDK
```
git clone https://github.com/raspberrypi/pico-sdk.git --recurse-submodules
cd pico-sdk
git submodule update --init
export PICO_SDK_PATH="$(pwd)"
```

Add the environment variable to your shell so it persists:
```
echo 'export PICO_SDK_PATH="$HOME/pico-sdk"' >> ~/.zshrc
```
Create Project and Copy Source Files
```
mkdir -p ~/projects/atari_adapter
cd ~/projects/atari_adapter
```
Copy these four files into that directory:

	•	CMakeLists.txt
  
	•	tusb_config.h
  
	•	usb_descriptors.c
  
	•	main.c

(Contents are in the source files in this repository.)
Then copy the SDK import helper:
```
cp $PICO_SDK_PATH/external/pico_sdk_import.cmake .
```
Build
```
mkdir build && cd build
cmake ..
make
```
This produces atari_adapter.uf2 in the build directory.

Flash

	1	Hold BOOTSEL while connecting the board via USB
  
	2	It mounts as a mass storage drive called RPI-RP2
  
	3	Drag atari_adapter.uf2 onto the drive
  
	4	It auto-reboots and enumerates as a USB gamepad
