import 'dart:async';
import 'dart:convert';

import 'package:flutter/foundation.dart';
import 'package:flutter_reactive_ble/flutter_reactive_ble.dart';

import '../models/config_model.dart';
import 'package:permission_handler/permission_handler.dart';

/// Servicio BLE para comunicación con el reloj Nexus Halo.
///
/// Gestiona:
/// - Escaneo y descubrimiento del dispositivo "Nexus Halo"
/// - Conexión/reconexión automática
/// - Escritura de bearing y distance (app → reloj)
/// - Lectura de haptic_tx, battery, radar_active (reloj → app)
/// - Escritura de haptic_rx y config (app → reloj)
/// - Calibración del wake-on-motion
class BleService extends ChangeNotifier {
  final FlutterReactiveBle _ble = FlutterReactiveBle();

  // ── BLE UUIDs (deben coincidir con firmware/ble_handler.cpp) ────────────
  // Nota: En firmware se usan UUIDs simplificados de 16-bit.
  // flutter_reactive_ble requiere UUIDs de 128-bit.
  // Los 16-bit se expanden a: 0000XXXX-0000-1000-8000-00805f9b34fb

  static final Uuid _serviceUuid =
      Uuid.parse('4a5c180a-5f2d-4e1b-822c-4a2d87b4c85b');

  static final Uuid _bearingCharUuid =
      Uuid.parse('4a5c2a58-5f2d-4e1b-822c-4a2d87b4c85b');

  static final Uuid _distanceCharUuid =
      Uuid.parse('4a5c2a59-5f2d-4e1b-822c-4a2d87b4c85b');

  static final Uuid _hapticTxCharUuid =
      Uuid.parse('4a5c2a5a-5f2d-4e1b-822c-4a2d87b4c85b');

  static final Uuid _hapticRxCharUuid =
      Uuid.parse('4a5c2a5b-5f2d-4e1b-822c-4a2d87b4c85b');

  static final Uuid _batteryCharUuid =
      Uuid.parse('4a5c2a19-5f2d-4e1b-822c-4a2d87b4c85b');

  static final Uuid _calibCmdCharUuid =
      Uuid.parse('4a5c2a5c-5f2d-4e1b-822c-4a2d87b4c85b');

  static final Uuid _calibStatusCharUuid =
      Uuid.parse('4a5c2a5d-5f2d-4e1b-822c-4a2d87b4c85b');

  static final Uuid _calibThresholdCharUuid =
      Uuid.parse('4a5c2a5e-5f2d-4e1b-822c-4a2d87b4c85b');

  static final Uuid _configCharUuid =
      Uuid.parse('4a5c2a60-5f2d-4e1b-822c-4a2d87b4c85b');

  static final Uuid _otaCharUuid =
      Uuid.parse('4a5c2a61-5f2d-4e1b-822c-4a2d87b4c85b');

  /// UUID para sincronización de hora (escribe Unix timestamp uint32 LE)
  static final Uuid _timeSyncCharUuid =
      Uuid.parse('4a5c2a2b-5f2d-4e1b-822c-4a2d87b4c85b');

  // ── Estado ──────────────────────────────────────────────────────────────

  /// Estado actual de la conexión BLE
  BleConnectionState _connectionState = BleConnectionState.disconnected;
  BleConnectionState get connectionState => _connectionState;

  /// ID del dispositivo conectado (MAC address)
  String? _connectedDeviceId;
  String? get connectedDeviceId => _connectedDeviceId;

  /// Último porcentaje de batería recibido del reloj
  int _batteryPercent = -1;
  int get batteryPercent => _batteryPercent;

  /// ¿El reloj está en RADAR_MODE?
  bool _radarModeActive = false;
  bool get radarModeActive => _radarModeActive;

  /// Último bearing enviado al reloj
  double _lastBearingSent = 0;
  double get lastBearingSent => _lastBearingSent;

  /// Última distancia enviada al reloj (metros)
  int _lastDistanceSent = 0;
  int get lastDistanceSent => _lastDistanceSent;

  // ── Streams internos ───────────────────────────────────────────────────

  StreamSubscription<ConnectionStateUpdate>? _connectionSubscription;
  StreamSubscription<DiscoveredDevice>? _scanSubscription;
  StreamSubscription<List<int>>? _hapticTxSubscription;
  StreamSubscription<List<int>>? _batterySubscription;
  StreamSubscription<List<int>>? _calibStatusSubscription;
  StreamSubscription<List<int>>? _calibThresholdSubscription;

  int? _calibThreshold;
  int? get calibThreshold => _calibThreshold;

  // ── Callbacks externos ─────────────────────────────────────────────────

  /// Callback cuando el reloj notifica un toque háptico (HAPTIC_TX)
  VoidCallback? onHapticTxReceived;

  /// Callback cuando cambia el estado de RADAR_MODE
  ValueChanged<bool>? onRadarModeChanged;

  /// Callback cuando cambia el porcentaje de batería
  ValueChanged<int>? onBatteryChanged;

  /// Callback con progreso de calibración (0-255)
  ValueChanged<int>? onCalibrationProgress;

  // ── Lifecycle ──────────────────────────────────────────────────────────

  /// Inicializar el servicio BLE.
  /// Debe llamarse una vez al inicio de la app.
  void initialize() {
    print('[BLE] Service initialized');
  }

  /// Liberar recursos
  @override
  void dispose() {
    _scanSubscription?.cancel();
    _connectionSubscription?.cancel();
    _hapticTxSubscription?.cancel();
    _batterySubscription?.cancel();
    _calibStatusSubscription?.cancel();
    super.dispose();
  }

  // ── Escaneo ────────────────────────────────────────────────────────────

  /// Dispositivos descubiertos durante el escaneo
  final List<DiscoveredDevice> _discoveredDevices = [];
  List<DiscoveredDevice> get discoveredDevices =>
      List.unmodifiable(_discoveredDevices);

  /// ¿Está escaneando actualmente?
  bool _isScanning = false;
  bool get isScanning => _isScanning;

  /// Iniciar escaneo BLE.
  /// Filtra por dispositivos que anuncian nuestro servicio UUID
  /// o que tienen el nombre "Nexus Halo" o similar.
  Future<void> startScan() async {
    if (_isScanning) return;

    Map<Permission, PermissionStatus> statuses = await [
      Permission.bluetoothScan,
      Permission.bluetoothConnect,
      Permission.locationWhenInUse,
    ].request();

    if (statuses[Permission.bluetoothScan] != PermissionStatus.granted ||
        statuses[Permission.locationWhenInUse] != PermissionStatus.granted) {
      print('[BLE] Permissions denied');
      return;
    }

    _discoveredDevices.clear();
    _isScanning = true;
    notifyListeners();

    print('[BLE] Starting scan...');

    _scanSubscription = _ble.scanForDevices(
      withServices: [_serviceUuid],
      scanMode: ScanMode.lowLatency,
    ).listen(
      (device) {
        // Filtrar por nombre o servicio
        final isOurDevice = device.name.contains('Nexus') ||
            device.name.contains('Halo') ||
            device.name.contains('Jessi') ||
            device.name.contains('Couples') ||
            device.serviceUuids.contains(_serviceUuid);

        if (isOurDevice) {
          // Evitar duplicados
          final exists = _discoveredDevices.any((d) => d.id == device.id);
          if (!exists) {
            _discoveredDevices.add(device);
            print('[BLE] Found device: ${device.name} (${device.id})');
            notifyListeners();
          }
        }
      },
      onError: (error) {
        print('[BLE] Scan error: $error');
        _isScanning = false;
        notifyListeners();
      },
    );

    // Auto-detener escaneo después de 15 segundos
    Future.delayed(const Duration(seconds: 15), () {
      if (_isScanning) {
        stopScan();
      }
    });
  }

  /// Detener escaneo BLE
  void stopScan() {
    _scanSubscription?.cancel();
    _scanSubscription = null;
    _isScanning = false;
    notifyListeners();
    print('[BLE] Scan stopped');
  }

  // ── Conexión ───────────────────────────────────────────────────────────

  /// Conectar a un dispositivo por su ID (MAC address)
  void connectToDevice(String deviceId) {
    print('[BLE] Connecting to device: $deviceId');

    _connectionState = BleConnectionState.connecting;
    _connectedDeviceId = deviceId;
    notifyListeners();

    // Cancelar escaneo si estaba activo
    stopScan();

    _connectionSubscription = _ble
        .connectToDevice(
      id: deviceId,
      connectionTimeout: const Duration(seconds: 10),
    )
        .listen(
      (update) {
        print('[BLE] Connection state: ${update.connectionState}');

        switch (update.connectionState) {
          case DeviceConnectionState.connected:
            _connectionState = BleConnectionState.connected;
            _onConnected(deviceId);
            break;
          case DeviceConnectionState.disconnected:
            _connectionState = BleConnectionState.disconnected;
            _onDisconnected();
            break;
          case DeviceConnectionState.connecting:
            _connectionState = BleConnectionState.connecting;
            break;
          case DeviceConnectionState.disconnecting:
            _connectionState = BleConnectionState.disconnecting;
            break;
        }

        notifyListeners();
      },
      onError: (error) {
        print('[BLE] Connection error: $error');
        _connectionState = BleConnectionState.disconnected;
        notifyListeners();

        // Reintentar conexión después de 5 segundos
        Future.delayed(const Duration(seconds: 5), () {
          if (_connectionState == BleConnectionState.disconnected &&
              _connectedDeviceId != null) {
            print('[BLE] Retrying connection...');
            connectToDevice(_connectedDeviceId!);
          }
        });
      },
    );
  }

  /// Desconectar del dispositivo actual
  void disconnect() {
    _connectionSubscription?.cancel();
    _connectionSubscription = null;
    _hapticTxSubscription?.cancel();
    _batterySubscription?.cancel();
    _calibStatusSubscription?.cancel();
    _calibThresholdSubscription?.cancel();
    _connectionState = BleConnectionState.disconnected;
    _connectedDeviceId = null;

    notifyListeners();
    print('[BLE] Disconnected');
  }

  /// Llamado al establecer conexión exitosa
  void _onConnected(String deviceId) {
    print('[BLE] Connected! Setting up subscriptions...');

    // Suscribirse a notificaciones del reloj
    _subscribeToHapticTx(deviceId);
    _subscribeToBattery(deviceId);
    _subscribeToCalibStatus(deviceId);
    _subscribeToCalibThreshold(deviceId);

    // Sincronizar la hora real al reloj (pequeño delay para que el stack BLE
    // termine de inicializar las características antes del primer write)
    Future.delayed(const Duration(milliseconds: 500), () {
      syncTime();
    });
  }

  /// Llamado al perder la conexión
  void _onDisconnected() {
    _hapticTxSubscription?.cancel();
    _batterySubscription?.cancel();
    _calibStatusSubscription?.cancel();
    _calibThresholdSubscription?.cancel();
    _radarModeActive = false;
    _batteryPercent = -1;
    _calibThreshold = null;
    print('[BLE] Disconnected, subscriptions cancelled');
  }

  // ── Subscripciones a notificaciones del reloj ──────────────────────────

  /// Suscribirse a HAPTIC_TX_CHAR (reloj notifica que el usuario tocó)
  void _subscribeToHapticTx(String deviceId) {
    final characteristic = QualifiedCharacteristic(
      serviceId: _serviceUuid,
      characteristicId: _hapticTxCharUuid,
      deviceId: deviceId,
    );

    _hapticTxSubscription =
        _ble.subscribeToCharacteristic(characteristic).listen(
      (data) {
        if (data.isNotEmpty && data[0] == 0x01) {
          print('[BLE] Haptic TX received! User tapped the watch.');
          onHapticTxReceived?.call();
        }
      },
      onError: (error) {
        print('[BLE] Haptic TX subscription error: $error');
      },
    );
  }

  /// Suscribirse a BATTERY_CHAR (reloj notifica % batería)
  void _subscribeToBattery(String deviceId) {
    final characteristic = QualifiedCharacteristic(
      serviceId: _serviceUuid,
      characteristicId: _batteryCharUuid,
      deviceId: deviceId,
    );

    _batterySubscription =
        _ble.subscribeToCharacteristic(characteristic).listen(
      (data) {
        if (data.isNotEmpty) {
          _batteryPercent = data[0];
          print('[BLE] Battery: $_batteryPercent%');
          onBatteryChanged?.call(_batteryPercent);
          notifyListeners();
        }
      },
      onError: (error) {
        print('[BLE] Battery subscription error: $error');
      },
    );
  }

  // ── Escritura al reloj ─────────────────────────────────────────────────

  /// Sincronizar la hora real del reloj.
  ///
  /// Envía el Unix timestamp actual (segundos desde epoch 1970-01-01 UTC)
  /// como uint32 little-endian. El reloj lo usa para mostrar la hora real
  /// en lugar del tiempo de uptime desde el arranque.
  /// Se llama automáticamente al conectar y puede llamarse manualmente.
  Future<void> syncTime() async {
    if (_connectionState != BleConnectionState.connected ||
        _connectedDeviceId == null) {
      return;
    }

    final characteristic = QualifiedCharacteristic(
      serviceId: _serviceUuid,
      characteristicId: _timeSyncCharUuid,
      deviceId: _connectedDeviceId!,
    );

    // Tiempo local ajustado a segundos para el reloj
    final now = DateTime.now();
    final int nowEpoch =
        (now.millisecondsSinceEpoch ~/ 1000) + now.timeZoneOffset.inSeconds;

    final bytes = ByteData(4);
    bytes.setUint32(0, nowEpoch, Endian.little);

    try {
      await _ble.writeCharacteristicWithResponse(
        characteristic,
        value: bytes.buffer.asUint8List(),
      );
      print('[BLE] Time synced: $nowEpoch '
          '(${DateTime.fromMillisecondsSinceEpoch(nowEpoch * 1000).toIso8601String()})');
    } catch (e) {
      print('[BLE] Time sync failed: $e');
    }
  }

  /// Enviar bearing al reloj (BEARING_CHAR, float 4 bytes).
  ///
  /// [bearing] es el rumbo absoluto hacia la pareja en grados [0, 360).
  Future<void> writeBearing(double bearing) async {
    if (_connectionState != BleConnectionState.connected ||
        _connectedDeviceId == null) {
      return;
    }

    final characteristic = QualifiedCharacteristic(
      serviceId: _serviceUuid,
      characteristicId: _bearingCharUuid,
      deviceId: _connectedDeviceId!,
    );

    // Convertir float a 4 bytes (little-endian IEEE 754)
    final bytes = ByteData(4);
    bytes.setFloat32(0, bearing, Endian.little);

    await _ble.writeCharacteristicWithResponse(
      characteristic,
      value: bytes.buffer.asUint8List(),
    );

    _lastBearingSent = bearing;
    print('[BLE] Bearing written: ${bearing.toStringAsFixed(1)}°');
  }

  /// Enviar distancia al reloj (DISTANCE_CHAR, uint32 4 bytes).
  ///
  /// [distanceMeters] es la distancia en metros.
  Future<void> writeDistance(int distanceMeters) async {
    if (_connectionState != BleConnectionState.connected ||
        _connectedDeviceId == null) {
      return;
    }

    final characteristic = QualifiedCharacteristic(
      serviceId: _serviceUuid,
      characteristicId: _distanceCharUuid,
      deviceId: _connectedDeviceId!,
    );

    // Convertir uint32 a 4 bytes (little-endian)
    final bytes = ByteData(4);
    bytes.setUint32(0, distanceMeters, Endian.little);

    await _ble.writeCharacteristicWithResponse(
      characteristic,
      value: bytes.buffer.asUint8List(),
    );

    _lastDistanceSent = distanceMeters;
    print('[BLE] Distance written: ${distanceMeters}m');
  }

  /// Enviar comando háptico al reloj (HAPTIC_RX_CHAR).
  ///
  /// El reloj vibrará con el patrón configurado.
  /// Se llama cuando la pareja envía un toque via Supabase.
  Future<void> sendHapticCommand() async {
    if (_connectionState != BleConnectionState.connected ||
        _connectedDeviceId == null) {
      return;
    }

    final characteristic = QualifiedCharacteristic(
      serviceId: _serviceUuid,
      characteristicId: _hapticRxCharUuid,
      deviceId: _connectedDeviceId!,
    );

    await _ble.writeCharacteristicWithResponse(
      characteristic,
      value: [0x01],
    );

    print('[BLE] Haptic RX command sent to watch');
  }

  /// Enviar configuración al reloj (CONFIG_CHAR, JSON comprimido).
  ///
  /// [config] es el modelo de configuración con todos los parámetros.
  Future<void> writeConfig(WatchConfig config) async {
    if (_connectionState != BleConnectionState.connected ||
        _connectedDeviceId == null) {
      return;
    }

    final characteristic = QualifiedCharacteristic(
      serviceId: _serviceUuid,
      characteristicId: _configCharUuid,
      deviceId: _connectedDeviceId!,
    );

    try {
      final jsonStr = jsonEncode(config.toBleJson());
      await _ble.writeCharacteristicWithResponse(
        characteristic,
        value: utf8.encode(jsonStr),
      );
      print('[BLE] Config written: $jsonStr');
    } catch (e) {
      print('[BLE] Error writing config: $e');
    }
  }

  /// Trigger OTA update — reboots watch into DFU bootloader mode.
  ///
  /// After calling this, the watch disconnects and restarts as a DFU device.
  /// Use a DFU library (nordic_dfu) to then upload the new firmware.
  Future<void> triggerOTA() async {
    if (_connectionState != BleConnectionState.connected ||
        _connectedDeviceId == null) {
      return;
    }

    final characteristic = QualifiedCharacteristic(
      serviceId: _serviceUuid,
      characteristicId: _otaCharUuid,
      deviceId: _connectedDeviceId!,
    );

    try {
      await _ble.writeCharacteristicWithResponse(
        characteristic,
        value: Uint8List.fromList([0x01]),
      );
      print('[BLE] OTA trigger sent — watch rebooting into DFU mode');
    } catch (e) {
      print('[BLE] Error triggering OTA: $e');
    }
  }

  // ── Calibración ────────────────────────────────────────────────────────

  /// Iniciar calibración de wake-on-motion
  Future<void> startCalibration() async {
    if (_connectionState != BleConnectionState.connected ||
        _connectedDeviceId == null) {
      return;
    }

    final characteristic = QualifiedCharacteristic(
      serviceId: _serviceUuid,
      characteristicId: _calibCmdCharUuid,
      deviceId: _connectedDeviceId!,
    );

    await _ble.writeCharacteristicWithResponse(
      characteristic,
      value: [0x01], // START
    );

    print('[BLE] Calibration START sent');
  }

  /// Cancelar calibración en curso
  Future<void> cancelCalibration() async {
    if (_connectionState != BleConnectionState.connected ||
        _connectedDeviceId == null) {
      return;
    }

    final characteristic = QualifiedCharacteristic(
      serviceId: _serviceUuid,
      characteristicId: _calibCmdCharUuid,
      deviceId: _connectedDeviceId!,
    );

    await _ble.writeCharacteristicWithResponse(
      characteristic,
      value: [0x03], // CANCEL
    );

    _calibStatusSubscription?.cancel();
    print('[BLE] Calibration CANCEL sent');
  }

  /// Escribir umbral de wake-on-motion
  Future<void> writeWakeThreshold(int threshold) async {
    if (_connectionState != BleConnectionState.connected ||
        _connectedDeviceId == null) {
      return;
    }

    final characteristic = QualifiedCharacteristic(
      serviceId: _serviceUuid,
      characteristicId: _calibThresholdCharUuid,
      deviceId: _connectedDeviceId!,
    );

    await _ble.writeCharacteristicWithResponse(
      characteristic,
      value: [threshold.clamp(0, 255)],
    );

    print('[BLE] Wake threshold written: 0x${threshold.toRadixString(16)}');
  }

  /// Suscribirse al progreso de calibración
  void _subscribeToCalibStatus(String deviceId) {
    _calibStatusSubscription?.cancel();
    final characteristic = QualifiedCharacteristic(
      serviceId: _serviceUuid,
      characteristicId: _calibStatusCharUuid,
      deviceId: deviceId,
    );

    _calibStatusSubscription =
        _ble.subscribeToCharacteristic(characteristic).listen(
      (data) {
        if (data.isNotEmpty) {
          final progress = data[0]; // 0-255
          print('[BLE] Calibration progress: $progress/255');
          onCalibrationProgress?.call(progress);
        }
      },
      onError: (error) {
        print('[BLE] Calibration status error: $error');
      },
    );
  }

  /// Suscribirse al umbral de calibración calculado por el reloj
  void _subscribeToCalibThreshold(String deviceId) {
    _calibThresholdSubscription?.cancel();
    final characteristic = QualifiedCharacteristic(
      serviceId: _serviceUuid,
      characteristicId: _calibThresholdCharUuid,
      deviceId: deviceId,
    );

    _calibThresholdSubscription =
        _ble.subscribeToCharacteristic(characteristic).listen(
      (data) {
        if (data.isNotEmpty) {
          _calibThreshold = data[0];
          print('[BLE] Calibration threshold received: $_calibThreshold');
          notifyListeners();
        }
      },
      onError: (error) {
        print('[BLE] Calibration threshold error: $error');
      },
    );
  }
}

/// Estados de conexión BLE simplificados
enum BleConnectionState {
  disconnected,
  connecting,
  connected,
  disconnecting,
}
