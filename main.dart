import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:permission_handler/permission_handler.dart';

void main() {
  runApp(const MyApp());
}

class MyApp extends StatelessWidget {
  const MyApp({super.key});
  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'BLE Wristband Monitor',
      theme: ThemeData(primarySwatch: Colors.teal),
      home: const WristbandMonitorPage(),
    );
  }
}

class WristbandMonitorPage extends StatefulWidget {
  const WristbandMonitorPage({super.key});
  @override
  State<WristbandMonitorPage> createState() => _WristbandMonitorPageState();
}

class _WristbandMonitorPageState extends State<WristbandMonitorPage> {
  BluetoothDevice? connectedDevice;
  BluetoothCharacteristic? healthCharacteristic;
  String bleStatus = "Disconnected";
  String heartRate = "--";
  String spo2 = "--";
  String temperature = "--";
  String ecg = "--";
  String leadOff = "--";
  String activityStatus = "--";

  final String serviceUuid = "4fafc201-1fb5-459e-8fcc-c5c9c331914b";
  final String charUuid = "beb5483e-36e1-4688-b7f5-ea07361b26a8";

  @override
  void initState() {
    super.initState();
    _initBLE();
  }

  Future<void> _initBLE() async {
    // Request all relevant permissions
    await Permission.bluetoothScan.request();
    await Permission.bluetoothConnect.request();
    await Permission.locationWhenInUse.request();
    await FlutterBluePlus.adapterState.where((state) => state == BluetoothAdapterState.on).first;
    _startScan();
  }

  void _startScan() {
    setState(() {
      bleStatus = "Scanning...";
    });
    FlutterBluePlus.startScan(timeout: const Duration(seconds: 5));
    FlutterBluePlus.scanResults.listen((results) async {
      for (var r in results) {
        if (r.device.name == "Wristband") {
          await FlutterBluePlus.stopScan();
          try {
            await r.device.connect(timeout: const Duration(seconds: 10));
            connectedDevice = r.device;
            setState(() {
              bleStatus = "Connected to Wristband";
            });
            await _discoverServices();
          } catch (e) {
            setState(() {
              bleStatus = "Connection failed";
            });
          }
          break;
        }
      }
    });
  }

  Future<void> _discoverServices() async {
    if (connectedDevice == null) return;
    List<BluetoothService> services = await connectedDevice!.discoverServices();
    for (var service in services) {
      if (service.uuid.str.toLowerCase() == serviceUuid) {
        for (var char in service.characteristics) {
          if (char.uuid.str.toLowerCase() == charUuid && char.properties.notify) {
            healthCharacteristic = char;
            await char.setNotifyValue(true);
            char.onValueReceived.listen((value) {
              _handleData(value);
            });
            setState(() {
              bleStatus = "Receiving data...";
            });
            return;
          }
        }
      }
    }
    setState(() {
      bleStatus = "Characteristic not found";
    });
  }

  void _handleData(List<int> data) {
    try {
      String payload = utf8.decode(data);
      // Example: HR:80,SpO2:97,Temp:36.8,ECG:123,LeadOff:0,Act:Idle
      var items = payload.split(',');
      String hr = "--", sp = "--", temp = "--", ecgVal = "--", lead = "--", act = "--";
      for (var item in items) {
        var pair = item.split(":");
        if (pair.length == 2) {
          switch (pair[0]) {
            case "HR":
              hr = pair[1];
              break;
            case "SpO2":
              sp = pair[1];
              break;
            case "Temp":
              temp = pair[1];
              break;
            case "ECG":
              ecgVal = pair[1];
              break;
            case "LeadOff":
              lead = pair[1] == "1" ? "Yes" : "No";
              break;
            case "Act":
              act = pair[1];
              break;
          }
        }
      }
      setState(() {
        heartRate = hr;
        spo2 = sp;
        temperature = temp;
        ecg = ecgVal;
        leadOff = lead;
        activityStatus = act;
      });
    } catch (e) {
      setState(() {
        bleStatus = "Data parse error";
      });
    }
  }

  Widget _buildCard(String label, String value, IconData icon, Color color) {
    return Card(
      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(16)),
      elevation: 5,
      margin: const EdgeInsets.symmetric(vertical: 8, horizontal: 16),
      child: ListTile(
        leading: Icon(icon, color: color, size: 32),
        title: Text(label, style: const TextStyle(fontSize: 18, fontWeight: FontWeight.bold)),
        subtitle: Text(value, style: const TextStyle(fontSize: 22, color: Colors.black87)),
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text("BLE Wristband Monitor")),
      body: RefreshIndicator(
        onRefresh: () async {
          if (connectedDevice != null) {
            await connectedDevice!.disconnect();
            connectedDevice = null;
          }
          _startScan();
        },
        child: ListView(
          children: [
            const SizedBox(height: 16),
            Center(child: Text("BLE Status: $bleStatus", style: const TextStyle(fontSize: 16))),
            _buildCard("Heart Rate", "$heartRate bpm", Icons.favorite, Colors.red),
            _buildCard("SpO₂", "$spo2%", Icons.bloodtype, Colors.blue),
            _buildCard("Temperature", "$temperature °C", Icons.thermostat, Colors.orange),
            _buildCard("ECG", ecg, Icons.monitor_heart, Colors.purple),
            _buildCard("Lead Off", leadOff, Icons.power_off, Colors.brown),
            _buildCard("Activity", activityStatus, Icons.directions_run, Colors.green),
          ],
        ),
      ),
    );
  }
}
