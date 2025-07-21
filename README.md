# Smart-Health-Monitoring-Wristband
Designed and developed a wearable health monitoring device, worn on the wrist. 
# BLE Wristband Monitor

A Flutter app to connect to the Seed Studio ESP32C3 'Wristband' via BLE and display sensor data (Heart Rate, SpO₂, Temperature, ECG, Activity, Lead-Off).

## Features
- Scans for BLE device named `Wristband`.
- Connects and subscribes to notifications from the correct service/characteristic.
- Parses and displays all sensor values in real time.
- Robust BLE connection and error handling.

## How to Run
1. Ensure your ESP32C3 is running the provided Arduino code and advertising as `Wristband`.
2. Clone this repo or copy to your Flutter projects directory.
3. Run:
   ```
   flutter pub get
   flutter run
   ```
4. Grant all requested permissions on your Android device.

## Notes
- BLE must be enabled on your phone.
- For Android 12+, all runtime permissions must be accepted.
- If you have connection issues, pull to refresh in the app to rescan and reconnect.

---

**BLE Service UUID:** `4fafc201-1fb5-459e-8fcc-c5c9c331914b`

**Characteristic UUID:** `beb5483e-36e1-4688-b7f5-ea07361b26a8`

---

For any issues, please check your ESP32 BLE status and ensure no other device is connected to the wristband.
