# Android Auto on Linux (OpenAuto Based)

A fully open-source Android Auto head unit implementation running on Linux using **Qt, GStreamer, BlueZ, and OpenAuto**.

This project evaluates and demonstrates the integration of Android Auto on Linux with both **wired and wireless projection support**.

---

## ✨ Features

- ✅ Wired Android Auto (USB)
- ✅ Audio routing from Android phone to Linux audio system
- ✅ Navigation and media rendering
- ✅ Video decoding via GStreamer pipeline
- ✅ Bluetooth pairing and telephony support
- ⚠️ Wireless Android Auto (experimental / in progress)

---

## Architecture

The system is built using:

- **OpenAuto** → Android Auto protocol implementation
- **AASDK** → Transport and service layer
- **Qt** → UI rendering and window management
- **GStreamer** → Video decoding and playback
- **BlueZ** → Bluetooth connectivity
- **PulseAudio** → Audio routing

### Video pipeline fix

To resolve the black-screen issue, QtMultimedia playback was replaced with:

---

## Dependencies

### System packages


sudo apt install -y \
build-essential cmake git pkg-config \
qtbase5-dev qtdeclarative5-dev qtconnectivity5-dev \
qtmultimedia5-dev libqt5multimedia5-plugins \
libusb-1.0-0-dev libssl-dev \
gstreamer1.0-tools gstreamer1.0-libav \
gstreamer1.0-plugins-base gstreamer1.0-plugins-good \
gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly \
bluez bluez-tools pulseaudio pulseaudio-module-bluetooth \
avahi-daemon avahi-utils

## 🔧 Build Critical Dependencies


### 1) Build Abseil (shared + PIC)

git clone https://github.com/abseil/abseil-cpp.git
cd abseil-cpp
mkdir build && cd build

cmake -DCMAKE_BUILD_TYPE=Release \
-DCMAKE_INSTALL_PREFIX=/usr/local \
-DBUILD_SHARED_LIBS=ON \
-DCMAKE_POSITION_INDEPENDENT_CODE=ON \
..

cmake --build . -j"$(nproc)"
sudo cmake --install .
sudo ldconfig

---


### 2) Build Protobuf v6.30.0 (shared + PIC)

git clone https://github.com/protocolbuffers/protobuf.git
cd protobuf
git checkout v6.30.0

mkdir build && cd build

cmake -DCMAKE_BUILD_TYPE=Release \
-Dprotobuf_BUILD_TESTS=OFF \
-DBUILD_SHARED_LIBS=ON \
-DCMAKE_POSITION_INDEPENDENT_CODE=ON \
-DCMAKE_INSTALL_PREFIX=/usr/local \
-DCMAKE_PREFIX_PATH=/usr/local \
..

cmake --build . -j"$(nproc)"
sudo cmake --install .
sudo ldconfig

---

## Build OpenAuto (with AASDK)

./build.sh release --with-aasdk --package

Run:

./build-release/bin/autoapp

---

## Permissions

sudo usermod -aG video,render,input,audio $USER

(Re-login required)

---

## 🧪 Test Cases

### 1-Wired Android Auto (USB)

Steps:

- Enable Android Developer Mode on phone
- Connect phone via USB
- Accept Android Auto permissions

Verify:

- UI renders on Linux
- Navigation UI loads
- Media playback works

---


###  2-Wireless Android Auto (Experimental)

Requirements:

- Same WiFi network for phone and Linux
- VPN disabled
- Avahi running
- Bluetooth paired
- Wireless Android Auto enabled in developer settings

Validation:

avahi-browse -rt _androidauto._tcp

Expected: head unit service visible

---

## Device & OS Compatibility

The solution was validated across multiple Android versions and devices.

### Tested Android Versions

- Xiaomi Redmi Note 10 Pro Max (Android 13)
- Samsung Galaxy A52S (Android 14)
- Samsung Galaxy A25 5G (Android 15)

### Tested Capabilities

- Wired Android Auto connection
- Audio routing
- Video rendering
- Navigation UI loading
- Media playback
- Bluetooth pairing
- Wireless discovery (experimental)


