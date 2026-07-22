import re

file_path = 'c:\\Users\\ovijo\\OneDrive\\Desktop\\Jessi\\app\\lib\\services\\ble_service.dart'

with open(file_path, 'r', encoding='utf-8') as f:
    content = f.read()

# 1. Add Imports
content = content.replace(
    "import 'package:permission_handler/permission_handler.dart';",
    "import 'package:permission_handler/permission_handler.dart';\nimport 'package:flutter_foreground_task/flutter_foreground_task.dart';\nimport 'package:shared_preferences/shared_preferences.dart';"
)

# 2. Add processBackgroundMessage and updateRole
new_methods = """
  // ── UI Proxy Methods ───────────────────────────────────────────────────

  void processBackgroundMessage(String message) {
    try {
      final map = jsonDecode(message) as Map<String, dynamic>;
      final type = map['type'] as String?;
      final payload = map['payload'] as Map<String, dynamic>? ?? {};

      if (type == 'ble_update') {
        final status = payload['status'] as String?;
        final device = payload['device'] as String?;
        if (device != null) _connectedDeviceId = device;
        
        if (status == 'connected') {
          _connectionState = BleConnectionState.connected;
        } else if (status == 'disconnected') {
          _connectionState = BleConnectionState.disconnected;
        } else if (status == 'connecting') {
          _connectionState = BleConnectionState.connecting;
        }
        notifyListeners();
      } else if (type == 'fullState') {
        final bleState = payload['bleState'] as String?;
        final mac = payload['macAddress'] as String?;
        if (mac != null && mac.isNotEmpty) _connectedDeviceId = mac;
        
        if (bleState == 'connected') {
          _connectionState = BleConnectionState.connected;
        } else if (bleState == 'disconnected') {
          _connectionState = BleConnectionState.disconnected;
        } else if (bleState == 'connecting') {
          _connectionState = BleConnectionState.connecting;
        }
        notifyListeners();
      } else if (type == 'session_expired') {
        disconnect();
      }
    } catch (e) {
      print('[BLE Proxy] Error parsing background message: $e');
    }
  }

  void updateRole(String role) {
    FlutterForegroundTask.sendDataToTask(jsonEncode({
      'type': 'update_role',
      'payload': {'role': role}
    }));
  }

"""
content = content.replace('  // ── Escaneo', new_methods + '  // ── Escaneo')

# 3. Modify connectToDevice
connect_impl = """void connectToDevice(String deviceId, {bool isRetry = false}) async {
    print('[BLE Proxy] Connecting to device: $deviceId (isRetry: $isRetry)');

    _connectionState = BleConnectionState.connecting;
    _connectedDeviceId = deviceId;
    notifyListeners();

    stopScan();

    final prefs = await SharedPreferences.getInstance();
    await prefs.setString('ble_mac_address', deviceId);

    FlutterForegroundTask.sendDataToTask(jsonEncode({
      'type': 'connect_ble',
      'payload': {'mac_address': deviceId}
    }));
  }"""

content = re.sub(r'void connectToDevice\(String deviceId, \{bool isRetry = false\}\) \{.*?(?=\n  void _scheduleReconnect\(\))', connect_impl + '\n', content, flags=re.DOTALL)

# 4. Modify disconnect
disconnect_impl = """void disconnect() {
    _scanTimer?.cancel();
    _connectionState = BleConnectionState.disconnected;
    _connectedDeviceId = null;
    notifyListeners();

    FlutterForegroundTask.sendDataToTask(jsonEncode({
      'type': 'stop_engine',
      'payload': {}
    }));
    print('[BLE Proxy] Disconnected / stop_engine sent');
  }"""

content = re.sub(r'void disconnect\(\) \{.*?(?=\n  void _cancelAllSubscriptions\(\))', disconnect_impl + '\n', content, flags=re.DOTALL)

# 5. Modify sendHapticCommand
haptic_impl = """Future<void> sendHapticCommand() async {
    FlutterForegroundTask.sendDataToTask(jsonEncode({
      'type': 'write_haptic',
      'payload': {'pattern': 'default'}
    }));
    print('[BLE Proxy] Haptic RX command sent to watch via background engine');
  }"""

content = re.sub(r'Future<void> sendHapticCommand\(\) async \{.*?(?=\n  /// Enviar configuración al reloj)', haptic_impl + '\n', content, flags=re.DOTALL)

with open(file_path, 'w', encoding='utf-8') as f:
    f.write(content)

print("Replacement complete")
