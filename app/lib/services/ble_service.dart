import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:device_info_plus/device_info_plus.dart';
import 'package:flutter/foundation.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart' as fbp;

import '../models/config_model.dart';
import 'package:permission_handler/permission_handler.dart';
import 'package:shared_preferences/shared_preferences.dart';
import 'foreground_service.dart';

class BleService extends ChangeNotifier {
  static final fbp.Guid _serviceUuid =
      fbp.Guid('4a5c180a-5f2d-4e1b-822c-4a2d87b4c85b');
  static final fbp.Guid _otaCharUuid =
      fbp.Guid('4a5c2a61-5f2d-4e1b-822c-4a2d87b4c85b');

  // State
  BleConnectionState _connectionState = BleConnectionState.disconnected;
  BleConnectionState get connectionState => _connectionState;

  String? _connectedDeviceId;
  String? get connectedDeviceId => _connectedDeviceId;

  int _batteryPercent = -1;
  int get batteryPercent => _batteryPercent;

  bool _radarModeActive = false;
  bool get radarModeActive => _radarModeActive;

  double _lastBearingSent = 0;
  double get lastBearingSent => _lastBearingSent;

  int _lastDistanceSent = 0;
  int get lastDistanceSent => _lastDistanceSent;

  int? _calibThreshold;
  int? get calibThreshold => _calibThreshold;

  final List<ScanResultDevice> _discoveredDevices = [];
  List<ScanResultDevice> get discoveredDevices => List.unmodifiable(_discoveredDevices);

  StreamSubscription<List<fbp.ScanResult>>? _scanSub;
  
  bool _isScanning = false;
  bool get isScanning => _isScanning;

  // Streams
  final _hapticTxController = StreamController<String>.broadcast();
  Stream<String> get hapticTxStream => _hapticTxController.stream;

  final _calibStatusController = StreamController<Map<String, int>>.broadcast();
  Stream<Map<String, int>> get calibStatusStream => _calibStatusController.stream;

  final _imuStreamController = StreamController<Map<String, int>>.broadcast();
  Stream<Map<String, int>> get imuStream => _imuStreamController.stream;

  final _compassStreamController = StreamController<double>.broadcast();
  Stream<double> get compassStream => _compassStreamController.stream;

  // Callbacks
  VoidCallback? onHapticTxReceived;
  ValueChanged<bool>? onRadarModeChanged;
  ValueChanged<int>? onBatteryChanged;
  ValueChanged<int>? onCalibrationProgress;

  Completer<void>? _otaReadyCompleter;
  bool _isOtaActive = false;

  BleService() {
    // Constructor
  }

  void initialize() {
    print('[BLE Proxy] Service initialized');
  }

  @override
  void dispose() {
    _scanSub?.cancel();
    _hapticTxController.close();
    _calibStatusController.close();
    _imuStreamController.close();
    _compassStreamController.close();
    super.dispose();
  }

  void processBackgroundMessage(String message) {
    try {
      final map = jsonDecode(message) as Map<String, dynamic>;
      final type = map['type'] as String?;
      final payload = map['payload'] as Map<String, dynamic>? ?? {};

      switch (type) {
        case 'ble_update':
          final status = payload['status'] as String?;
          final device = payload['device'] as String?;
          final error = payload['error'] as String?;
          if (device != null) { _connectedDeviceId = device; }
          if (status == 'connected') { _connectionState = BleConnectionState.connected; }
          else if (status == 'disconnected') {
            _connectionState = BleConnectionState.disconnected;
            _connectedDeviceId = null;
            if (error != null && error.isNotEmpty) {
              print('[BLE Proxy] Disconnect reason: $error');
            }
          }
          else if (status == 'connecting') { _connectionState = BleConnectionState.connecting; }
          notifyListeners();
          break;

        case 'fullState':
          final bleState = payload['bleState'] as String?;
          final mac = payload['macAddress'] as String?;
          if (bleState == 'disconnected') {
            _connectionState = BleConnectionState.disconnected;
            _connectedDeviceId = null;
          } else {
            if (mac != null && mac.isNotEmpty) { _connectedDeviceId = mac; }
            if (bleState == 'connected') { _connectionState = BleConnectionState.connected; }
            else if (bleState == 'connecting') { _connectionState = BleConnectionState.connecting; }
          }
          
          if (payload['batteryPercent'] != null) {
            _batteryPercent = payload['batteryPercent'] as int;
            onBatteryChanged?.call(_batteryPercent);
          }
          if (payload['radarModeActive'] != null) {
            _radarModeActive = payload['radarModeActive'] as bool;
            onRadarModeChanged?.call(_radarModeActive);
          }
          if (payload['calibThreshold'] != null) {
            _calibThreshold = payload['calibThreshold'] as int;
          }
          
          final pausedForOta = payload['paused_for_ota'] == true;
          if (pausedForOta && !_isOtaActive) {
            print('[BLE Proxy] Self-healing stale OTA pause state.');
            ForegroundService.sendCommand(jsonEncode({
              'type': 'resume_after_ota',
              'payload': {}
            }));
          }
          
          notifyListeners();
          break;

        case 'session_expired':
          disconnect();
          break;

        case 'haptic_tx_received':
          print('[BLE Proxy] Haptic TX received! User tapped the watch.');
          onHapticTxReceived?.call();
          _hapticTxController.add('tapped');
          break;

        case 'battery_update':
          _batteryPercent = payload['percent'] as int? ?? _batteryPercent;
          print('[BLE Proxy] Battery: $_batteryPercent%');
          onBatteryChanged?.call(_batteryPercent);
          notifyListeners();
          break;

        case 'radar_mode_update':
          _radarModeActive = payload['active'] == true;
          print('[BLE Proxy] Radar mode changed by watch: $_radarModeActive');
          onRadarModeChanged?.call(_radarModeActive);
          notifyListeners();
          break;

        case 'calib_status_update':
          if (payload['status'] != null) {
             int progress = 0;
             if (payload['status'] is Map) {
               progress = payload['status']['progress'] as int? ?? 0;
             } else if (payload['status'] is int) {
               progress = payload['status'] as int;
             }
             onCalibrationProgress?.call(progress);
             if (payload['status'] is Map) {
                _calibStatusController.add(Map<String, int>.from(payload['status'] as Map));
             } else {
                _calibStatusController.add({'progress': progress});
             }
          }
          break;

        case 'calib_threshold_update':
          _calibThreshold = payload['threshold'] as int?;
          print('[BLE Proxy] Calibration threshold received: $_calibThreshold');
          notifyListeners();
          break;

        case 'imu_stream_update':
          if (payload['data'] != null) {
            _imuStreamController.add(Map<String, int>.from(payload['data'] as Map));
          }
          break;

        case 'compass_stream_update':
          if (payload['heading'] != null) {
            _compassStreamController.add((payload['heading'] as num).toDouble());
          }
          break;

        case 'ota_ready':
          print('[BLE Proxy] Received ota_ready');
          if (_otaReadyCompleter != null && !_otaReadyCompleter!.isCompleted) {
            _otaReadyCompleter!.complete();
          }
          break;

        default:
          break;
      }
    } catch (e) {
      print('[BLE Proxy] Error parsing background message: $e');
    }
  }

  void updateRole(String role) {
    ForegroundService.sendCommand(jsonEncode({
      'type': 'update_role',
      'payload': {'role': role}
    }));
  }

  Future<void> startScan() async {
    final androidSdkInt = await _getAndroidSdkInt();
    final permissions = requiredPermissionsForScan(
      isAndroid: Platform.isAndroid,
      androidSdkInt: androidSdkInt ?? 30,
    );

    Map<Permission, PermissionStatus> statuses = await permissions.request();
    final hasRequiredPermissions = permissions.every(
      (permission) => statuses[permission] == PermissionStatus.granted,
    );

    if (!hasRequiredPermissions) {
      print('[BLE Proxy] Permissions denied');
      return;
    }

    _discoveredDevices.clear();
    _isScanning = true;
    notifyListeners();

    _scanSub?.cancel();
    _scanSub = fbp.FlutterBluePlus.scanResults.listen((results) {
      for (fbp.ScanResult r in results) {
        final name = r.device.advName.isNotEmpty ? r.device.advName : r.advertisementData.advName;
        if (name == 'Nexus Halo') {
          final exists = _discoveredDevices.any((d) => d.id == r.device.remoteId.str);
          if (!exists) {
            _discoveredDevices.add(ScanResultDevice(
              id: r.device.remoteId.str,
              name: name,
              rssi: r.rssi,
            ));
            notifyListeners();
          }
        }
      }
    }, onError: (Object e) {
      debugPrint('[BLE Service] scanForDevices error: $e');
    });
    
    await fbp.FlutterBluePlus.startScan(timeout: const Duration(seconds: 15));
  }

  void stopScan() {
    _isScanning = false;
    _scanSub?.cancel();
    fbp.FlutterBluePlus.stopScan();
    notifyListeners();
  }

  Future<int?> _getAndroidSdkInt() async {
    if (!Platform.isAndroid) return null;
    try {
      final info = await DeviceInfoPlugin().androidInfo;
      return info.version.sdkInt;
    } catch (e) {
      print('[BLE Proxy] Failed to read Android SDK version: $e');
      return null;
    }
  }

  @visibleForTesting
  static List<Permission> requiredPermissionsForScan({
    required bool isAndroid,
    required int androidSdkInt,
  }) {
    if (!isAndroid) {
      return [Permission.locationWhenInUse];
    }
    if (androidSdkInt >= 31) {
      return [
        Permission.bluetoothScan,
        Permission.bluetoothConnect,
        Permission.locationWhenInUse,
      ];
    }
    return [
      Permission.bluetooth,
      Permission.locationWhenInUse,
    ];
  }

  void connectToDevice(String deviceId, {bool isRetry = false}) async {
    _connectionState = BleConnectionState.connecting;
    _connectedDeviceId = deviceId;
    notifyListeners();
    stopScan();

    final prefs = await SharedPreferences.getInstance();
    await prefs.setString('ble_mac_address', deviceId);

    ForegroundService.sendCommand(jsonEncode({
      'type': 'connect_ble',
      'payload': {'mac_address': deviceId}
    }));
  }

  void disconnect() {
    _connectionState = BleConnectionState.disconnected;
    _connectedDeviceId = null;
    notifyListeners();
    ForegroundService.sendCommand(jsonEncode({
      'type': 'stop_engine',
      'payload': {}
    }));
  }

  Future<void> syncTime([int attempt = 0]) async {
    ForegroundService.sendCommand(jsonEncode({
      'type': 'sync_time',
      'payload': {}
    }));
  }

  Future<void> writeBearing(double bearing) async {
    _lastBearingSent = bearing;
    ForegroundService.sendCommand(jsonEncode({
      'type': 'write_bearing',
      'payload': {'bearing': bearing}
    }));
  }

  Future<void> writeDistance(int distanceMeters) async {
    _lastDistanceSent = distanceMeters;
    ForegroundService.sendCommand(jsonEncode({
      'type': 'write_distance',
      'payload': {'distance': distanceMeters}
    }));
  }

  Future<void> writeRadarMode(bool active) async {
    ForegroundService.sendCommand(jsonEncode({
      'type': 'write_radar_mode',
      'payload': {'active': active}
    }));
  }

  Future<void> sendHapticCommand() async {
    ForegroundService.sendCommand(jsonEncode({
      'type': 'write_haptic',
      'payload': {'pattern': 'default'}
    }));
  }

  Future<void> writeConfig(WatchConfig config) async {
    ForegroundService.sendCommand(jsonEncode({
      'type': 'write_config',
      'payload': {'config_json': jsonEncode(config.toBleJson())}
    }));
  }

  Future<void> triggerOTA() async {
    if (_connectedDeviceId == null) return;
    final deviceId = _connectedDeviceId!;

    _isOtaActive = true;
    _otaReadyCompleter = Completer<void>();

    try {
      print('[BLE Proxy] Discarding background connection for OTA...');
      ForegroundService.sendCommand(jsonEncode({
        'type': 'pause_for_ota',
        'payload': {}
      }));

      // Wait for background to acknowledge ota_ready
      await _otaReadyCompleter!.future.timeout(const Duration(seconds: 15));

      print('[BLE Proxy] ota_ready received. Starting local BLE connection for OTA...');
      
      final device = fbp.BluetoothDevice.fromId(deviceId);
      
      // We must connect locally to trigger the DFU mode
      await device.connect(timeout: const Duration(seconds: 10), autoConnect: false, license: fbp.License.nonprofit);
      
      final services = await device.discoverServices();
      fbp.BluetoothService? targetService;
      for (final s in services) {
        if (s.uuid == _serviceUuid) {
          targetService = s;
          break;
        }
      }
      
      if (targetService != null) {
        fbp.BluetoothCharacteristic? otaChar;
        for (final c in targetService.characteristics) {
          if (c.uuid == _otaCharUuid) {
            otaChar = c;
            break;
          }
        }
        
        if (otaChar != null) {
          await otaChar.write([0x01], withoutResponse: false);
          print('[BLE Proxy] OTA trigger sent — watch rebooting into DFU mode');
        }
      }
      
      await device.disconnect();

    } catch (e) {
      print('[BLE Proxy] Error during OTA process: $e');
    } finally {
      _isOtaActive = false;
      _otaReadyCompleter = null;
      print('[BLE Proxy] OTA process finished, resuming background connection...');
      ForegroundService.sendCommand(jsonEncode({
        'type': 'resume_after_ota',
        'payload': {}
      }));
    }
  }

  Future<void> sendCalibCmd(int cmd) async {
    ForegroundService.sendCommand(jsonEncode({
      'type': 'send_calib_cmd',
      'payload': {'cmd': cmd}
    }));
  }

  Future<void> startImuStream() => sendCalibCmd(0x05);
  Future<void> stopImuStream() => sendCalibCmd(0x06);
  Future<void> startCompassStream() => sendCalibCmd(0x07);
  Future<void> stopCompassStream() => sendCalibCmd(0x08);

  Future<void> startCalibration() async {
    ForegroundService.sendCommand(jsonEncode({
      'type': 'send_calib_cmd',
      'payload': {'cmd': 0x01}
    }));
  }

  Future<void> cancelCalibration() async {
    ForegroundService.sendCommand(jsonEncode({
      'type': 'send_calib_cmd',
      'payload': {'cmd': 0x03}
    }));
  }

  Future<void> startCompassCalibration() async {
    ForegroundService.sendCommand(jsonEncode({
      'type': 'send_calib_cmd',
      'payload': {'cmd': 0x04}
    }));
  }

  Future<void> writeWakeThreshold(int threshold) async {
    ForegroundService.sendCommand(jsonEncode({
      'type': 'write_wake_threshold',
      'payload': {'threshold': threshold}
    }));
  }

  @visibleForTesting
  static bool shouldScheduleReconnect({
    required bool isConnected,
    required int currentAttempt,
    required int maxAttempts,
  }) {
    if (isConnected) return false;
    if (maxAttempts < 0) return true;
    return currentAttempt < maxAttempts;
  }
}

enum BleConnectionState {
  disconnected,
  connecting,
  connected,
  disconnecting,
}

class ScanResultDevice {
  final String id;
  final String name;
  final int rssi;

  ScanResultDevice({required this.id, required this.name, required this.rssi});
}
