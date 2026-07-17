import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:device_info_plus/device_info_plus.dart';
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
      Uuid.parse('00002a19-0000-1000-8000-00805f9b34fb');

  static final Uuid _radarModeCharUuid =
      Uuid.parse('4a5c2a5f-5f2d-4e1b-822c-4a2d87b4c85b');

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

  static final Uuid _imuStreamCharUuid =
      Uuid.parse('4a5c2a62-5f2d-4e1b-822c-4a2d87b4c85b');

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
  
  final _hapticTxController = StreamController<String>.broadcast();
  Stream<String> get hapticTxStream => _hapticTxController.stream;

  final _calibStatusController = StreamController<Map<String, int>>.broadcast();
  Stream<Map<String, int>> get calibStatusStream => _calibStatusController.stream;

  final _imuStreamController = StreamController<Map<String, int>>.broadcast();
  Stream<Map<String, int>> get imuStream => _imuStreamController.stream;

  BleService() {
    // Constructor logic
  }

  StreamSubscription<List<int>>? _hapticTxSubscription;
  StreamSubscription<List<int>>? _batterySubscription;
  StreamSubscription<List<int>>? _calibStatusSubscription;
  StreamSubscription<List<int>>? _calibThresholdSubscription;
  StreamSubscription<List<int>>? _radarModeSubscription;
  StreamSubscription<List<int>>? _imuStreamSubscription;

  Timer? _scanTimer;
  Timer? _retryTimer;
  Timer? _timeSyncTimer;

  int? _calibThreshold;
  int? get calibThreshold => _calibThreshold;

  int _reconnectAttempt = 0;
  // Set to infinite, but implement an exponential backoff in _retryTimer
  static const int _maxReconnectAttempts = 20;

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
    _timeSyncTimer?.cancel();
    _scanTimer?.cancel();
    _retryTimer?.cancel();
    _scanSubscription?.cancel();
    _connectionSubscription?.cancel();
    _hapticTxSubscription?.cancel();
    _batterySubscription?.cancel();
    _calibStatusSubscription?.cancel();
    _calibThresholdSubscription?.cancel();
    _radarModeSubscription?.cancel();
    _imuStreamSubscription?.cancel();
    _hapticTxController.close();
    _calibStatusController.close();
    _imuStreamController.close();
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
    _isScanning = true;

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
      print('[BLE] Permissions denied');
      _isScanning = false;
      return;
    }

    _discoveredDevices.clear();
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
    _scanTimer?.cancel();
    _scanTimer = Timer(const Duration(seconds: 15), () {
      if (_isScanning) {
        stopScan();
      }
    });
  }

  Future<int?> _getAndroidSdkInt() async {
    if (!Platform.isAndroid) return null;

    try {
      final info = await DeviceInfoPlugin().androidInfo;
      return info.version.sdkInt;
    } catch (e) {
      print('[BLE] Failed to read Android SDK version: $e');
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

  /// Detener escaneo BLE
  void stopScan() {
    _scanTimer?.cancel();
    _scanSubscription?.cancel();
    _scanSubscription = null;
    _isScanning = false;
    notifyListeners();
    print('[BLE] Scan stopped');
  }

  // ── Conexión ───────────────────────────────────────────────────────────

  /// Conectar a un dispositivo por su ID (MAC address)
  void connectToDevice(String deviceId, {bool isRetry = false}) {
    print('[BLE] Connecting to device: $deviceId (isRetry: $isRetry)');

    if (!isRetry) {
      disconnect(); // Ensure any existing timers/subscriptions are canceled
      _reconnectAttempt = 0;
    } else {
      _connectionSubscription?.cancel();
    }

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
            _reconnectAttempt = 0;
            _connectionState = BleConnectionState.connected;
            _onConnected(deviceId);
            break;
          case DeviceConnectionState.disconnected:
            _connectionState = BleConnectionState.disconnected;
            _onDisconnected();
            _scheduleReconnect();
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
        if (_connectionState != BleConnectionState.disconnected) {
          _connectionState = BleConnectionState.disconnected;
          _onDisconnected();
          notifyListeners();
          _scheduleReconnect();
        }
      },
    );
  }

  void _scheduleReconnect() {
    if (!shouldScheduleReconnect(
      isConnected: false,
      currentAttempt: _reconnectAttempt,
      maxAttempts: _maxReconnectAttempts,
    )) {
      print('[BLE] Reconnect budget exhausted; stopping retries');
      return;
    }

    _reconnectAttempt += 1;

    // Reintentar conexión después de 5 segundos con backoff exponencial
    final backoffSeconds = 5 * (1 << (_reconnectAttempt > 6 ? 6 : _reconnectAttempt - 1));
    if (_retryTimer?.isActive ?? false) return;
    _retryTimer?.cancel();
    _retryTimer = Timer(Duration(seconds: backoffSeconds), () {
      if (_connectionState == BleConnectionState.disconnected &&
          _connectedDeviceId != null) {
        print('[BLE] Retrying connection (attempt $_reconnectAttempt)...');
        connectToDevice(_connectedDeviceId!, isRetry: true);
      }
    });
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

  /// Desconectar del dispositivo actual
  void disconnect() {
    _timeSyncTimer?.cancel();
    _retryTimer?.cancel();
    _scanTimer?.cancel();
    _connectionSubscription?.cancel();
    _connectionSubscription = null;
    _hapticTxSubscription?.cancel();
    _batterySubscription?.cancel();
    _calibStatusSubscription?.cancel();
    _calibThresholdSubscription?.cancel();
    _radarModeSubscription?.cancel();
    _imuStreamSubscription?.cancel();
    _connectionState = BleConnectionState.disconnected;
    _connectedDeviceId = null;

    notifyListeners();
    print('[BLE] Disconnected');
  }

  void _cancelAllSubscriptions() {
    _hapticTxSubscription?.cancel(); _hapticTxSubscription = null;
    _batterySubscription?.cancel(); _batterySubscription = null;
    _calibStatusSubscription?.cancel(); _calibStatusSubscription = null;
    _calibThresholdSubscription?.cancel(); _calibThresholdSubscription = null;
    _radarModeSubscription?.cancel(); _radarModeSubscription = null;
    _imuStreamSubscription?.cancel(); _imuStreamSubscription = null;
  }

  /// Llamado al establecer conexión exitosa
  Future<void> _onConnected(String deviceId) async {
    _cancelAllSubscriptions();
    print('[BLE] Connected! Setting up subscriptions...');

    // Retry mechanism for GATT operations post-connection (Race Condition Fix)
    // Some Android devices take a few milliseconds to fully populate the GATT table.
    int negotiatedMtu = -1;
    for (int i = 0; i < 3; i++) {
      await Future.delayed(const Duration(milliseconds: 500));
      try {
        negotiatedMtu = await _ble.requestMtu(deviceId: deviceId, mtu: 251);
        print('[BLE] MTU negotiated: $negotiatedMtu');
        break; // Success! GATT is ready.
      } catch (e) {
        print('[BLE] Failed to negotiate MTU (attempt ${i + 1}): $e');
      }
    }

    if (negotiatedMtu == -1) {
      print('[BLE] WARNING: GATT table failed to populate after retries. Subscriptions might fail.');
    }

    // Suscribirse a notificaciones del reloj
    _subscribeToHapticTx(deviceId);
    await Future.delayed(const Duration(milliseconds: 150));
    _subscribeToBattery(deviceId);
    await Future.delayed(const Duration(milliseconds: 150));
    _subscribeToCalibStatus(deviceId);
    await Future.delayed(const Duration(milliseconds: 150));
    _subscribeToCalibThreshold(deviceId);
    await Future.delayed(const Duration(milliseconds: 150));
    _subscribeToRadarMode(deviceId);
    await Future.delayed(const Duration(milliseconds: 150));
    _subscribeToImuStream(deviceId);
    await Future.delayed(const Duration(milliseconds: 150));

    // Leer la batería inicial directamente
    final batteryChar = QualifiedCharacteristic(
      serviceId: _serviceUuid,
      characteristicId: _batteryCharUuid,
      deviceId: deviceId,
    );
    _ble.readCharacteristic(batteryChar).then((data) {
      if (_connectionState != BleConnectionState.connected) return;
      if (data.isNotEmpty) {
        _batteryPercent = data[0];
        print('[BLE] Initial battery read: $_batteryPercent%');
        onBatteryChanged?.call(_batteryPercent);
        notifyListeners();
      }
    }).catchError((e) {
      print('[BLE] Failed to read initial battery: $e');
    });

    // Sincronizar la hora real al reloj (con retry exponencial nativo)
    syncTime();
  }

  /// Llamado al perder la conexión
  void _onDisconnected() {
    _timeSyncTimer?.cancel();
    _retryTimer?.cancel();
    _hapticTxSubscription?.cancel();
    _batterySubscription?.cancel();
    _calibStatusSubscription?.cancel();
    _calibThresholdSubscription?.cancel();
    _radarModeSubscription?.cancel();
    _imuStreamSubscription?.cancel();
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
          _hapticTxController.add('tapped');
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

  /// Suscribirse a RADAR_MODE_CHAR
  void _subscribeToRadarMode(String deviceId) {
    final characteristic = QualifiedCharacteristic(
      serviceId: _serviceUuid,
      characteristicId: _radarModeCharUuid,
      deviceId: deviceId,
    );

    _radarModeSubscription =
        _ble.subscribeToCharacteristic(characteristic).listen(
      (data) {
        if (data.isNotEmpty) {
          _radarModeActive = data[0] == 1;
          print('[BLE] Radar mode changed by watch: $_radarModeActive');
          onRadarModeChanged?.call(_radarModeActive);
          notifyListeners();
        }
      },
      onError: (error) {
        print('[BLE] Radar mode subscription error: $error');
      },
    );
  }

  // ── Escritura al reloj ─────────────────────────────────────────────────

  Future<void> _bleWriteQueue = Future<void>.value();

  Future<void> _enqueueWrite(Future<void> Function() writeOp) {
    _bleWriteQueue = _bleWriteQueue.catchError((_) {}).then((_) async {
      try {
        await writeOp().timeout(const Duration(seconds: 5));
      } catch (e) {
        print('[BLE] Write error/timeout: $e');
      }
    });
    return _bleWriteQueue;
  }

  /// Sincronizar la hora real del reloj.
  ///
  /// Envía el Unix timestamp UTC actual y el offset horario en segundos
  /// como uint32 e int32 little-endian (8 bytes en total).
  /// Se llama automáticamente al conectar e incluye reintentos y mantenimiento periódico.
  Future<void> syncTime([int attempt = 0]) async {
    if (_connectionState != BleConnectionState.connected ||
        _connectedDeviceId == null) {
      return;
    }

    final characteristic = QualifiedCharacteristic(
      serviceId: _serviceUuid,
      characteristicId: _timeSyncCharUuid,
      deviceId: _connectedDeviceId!,
    );

    final now = DateTime.now();
    final int nowEpoch = now.millisecondsSinceEpoch ~/ 1000;
    final int tzOffset = now.timeZoneOffset.inSeconds;

    final bytes = ByteData(8);
    // TODO(Y2038): nowEpoch is truncated to uint32. Firmware must be updated
    // to accept a uint64 timestamp before this can be fixed safely.
    bytes.setUint32(0, nowEpoch, Endian.little);
    bytes.setInt32(4, tzOffset, Endian.little);

    try {
      await _enqueueWrite(() => _ble.writeCharacteristicWithResponse(
        characteristic,
        value: bytes.buffer.asUint8List(0, 8),
      ));
      print('[BLE] Time synced: UTC $nowEpoch, TZ offset $tzOffset '
          '(${now.toUtc().toIso8601String()})');
      
      // Mantenimiento periódico cada 15 minutos para evitar el drift del reloj
      _timeSyncTimer?.cancel();
      _timeSyncTimer = Timer(const Duration(minutes: 15), () => syncTime());
    } catch (e) {
      print('[BLE] Time sync failed (attempt $attempt): $e');
      if (attempt < 5) {
        final delayMs = 500 * (1 << attempt); // Exponential backoff: 500ms, 1s, 2s, 4s, 8s
        _timeSyncTimer?.cancel();
        _timeSyncTimer = Timer(Duration(milliseconds: delayMs), () => syncTime(attempt + 1));
      } else {
        print('[BLE] Time sync max retries reached. Will retry in 15 minutes.');
        _timeSyncTimer?.cancel();
        _timeSyncTimer = Timer(const Duration(minutes: 15), () => syncTime());
      }
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

    try {
      await _enqueueWrite(() => _ble.writeCharacteristicWithResponse(
        characteristic,
        value: bytes.buffer.asUint8List(0, 4),
      ));

      _lastBearingSent = bearing;
      print('[BLE] Bearing written: ${bearing.toStringAsFixed(1)}°');
    } catch (e) {
      print('[BLE] Error writing bearing: $e');
    }
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

    try {
      await _enqueueWrite(() => _ble.writeCharacteristicWithResponse(
        characteristic,
        value: bytes.buffer.asUint8List(0, 4),
      ));

      _lastDistanceSent = distanceMeters;
      print('[BLE] Distance written: ${distanceMeters}m');
    } catch (e) {
      print('[BLE] Error writing distance: $e');
    }
  }

  /// Enviar estado de radar al reloj (RADAR_MODE_CHAR, 1 byte).
  Future<void> writeRadarMode(bool active) async {
    if (_connectionState != BleConnectionState.connected || _connectedDeviceId == null) return;
    try {
      final characteristic = QualifiedCharacteristic(
        serviceId: _serviceUuid,
        characteristicId: _radarModeCharUuid,
        deviceId: _connectedDeviceId!,
      );
      await _enqueueWrite(() => _ble.writeCharacteristicWithResponse(
        characteristic,
        value: [active ? 0x01 : 0x00],
      ));
      print('[BLE] Radar mode written: $active');
    } catch (e) {
      print('[BLE] Error writing radar mode: $e');
    }
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

    try {
      await _enqueueWrite(() => _ble.writeCharacteristicWithResponse(
        characteristic,
        value: [0x01],
      ));

      print('[BLE] Haptic RX command sent to watch');
    } catch (e) {
      print('[BLE] Error writing haptic command: $e');
    }
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
      await _enqueueWrite(() => _ble.writeCharacteristicWithResponse(
        characteristic,
        value: utf8.encode(jsonStr),
      ));
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
      await _enqueueWrite(() => _ble.writeCharacteristicWithResponse(
        characteristic,
        value: Uint8List.fromList([0x01]),
      ));
      print('[BLE] OTA trigger sent — watch rebooting into DFU mode');
    } catch (e) {
      print('[BLE] Error triggering OTA: $e');
    }
  }

  // ── Calibración ────────────────────────────────────────────────────────

  Future<void> sendCalibCmd(int cmd) async {
    if (_connectionState != BleConnectionState.connected || _connectedDeviceId == null) return;
    try {
      final characteristic = QualifiedCharacteristic(
        serviceId: _serviceUuid,
        characteristicId: _calibCmdCharUuid,
        deviceId: _connectedDeviceId!,
      );
      await _enqueueWrite(() => _ble.writeCharacteristicWithResponse(characteristic, value: [cmd]));
      print('[BLE] Calib CMD sent: 0x${cmd.toRadixString(16)}');
    } catch (e) {
      print('[BLE] Error sending Calib CMD: $e');
    }
  }

  /// Iniciar stream IMU
  Future<void> startImuStream() => sendCalibCmd(0x05);

  /// Detener stream IMU
  Future<void> stopImuStream() => sendCalibCmd(0x06);

  /// Iniciar calibración de wake-on-motion
  Future<void> startCalibration() async {
    if (_connectionState != BleConnectionState.connected ||
        _connectedDeviceId == null) {
      return;
    }

    _subscribeToCalibStatus(_connectedDeviceId!);
    try {
      await sendCalibCmd(0x01).timeout(const Duration(seconds: 15));
    } catch (e) {
      print('[BLE] Calibration start timeout/error: $e');
    }
    print('[BLE] Calibration START sent');
  }

  /// Cancelar calibración en curso
  Future<void> cancelCalibration() async {
    if (_connectionState != BleConnectionState.connected ||
        _connectedDeviceId == null) {
      return;
    }

    sendCalibCmd(0x03);
    _calibStatusSubscription?.cancel();
    _calibStatusSubscription = null;
    print('[BLE] Calibration CANCEL sent');
  }

  /// Iniciar calibración de la brújula (magnetómetro)
  Future<void> startCompassCalibration() async {
    if (_connectionState != BleConnectionState.connected ||
        _connectedDeviceId == null) {
      return;
    }

    try {
      await sendCalibCmd(0x04).timeout(const Duration(seconds: 15));
    } catch (e) {
      print('[BLE] Compass calibration start timeout/error: $e');
    }
    print('[BLE] Compass calibration START sent');
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

    try {
      await _enqueueWrite(() => _ble.writeCharacteristicWithResponse(
        characteristic,
        value: [threshold.clamp(0, 255)],
      ));

      print('[BLE] Wake threshold written: 0x${threshold.toRadixString(16)}');
    } catch (e) {
      print('[BLE] Error parsing calib threshold: $e');
    }
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

  void _subscribeToImuStream(String deviceId) {
    try {
      final characteristic = QualifiedCharacteristic(
        serviceId: _serviceUuid,
        characteristicId: _imuStreamCharUuid,
        deviceId: deviceId,
      );

      _imuStreamSubscription =
          _ble.subscribeToCharacteristic(characteristic).listen(
        (data) {
          if (data.length == 4) {
            int mg = data[0] | (data[1] << 8);
            int dps = data[2] | (data[3] << 8);
            
            // Si el MSB indica negativo para dps, hacemos sign extension
            if ((dps & 0x8000) != 0) {
              dps = dps - 0x10000;
            }

            _imuStreamController.add({
              'mg': mg,
              'dps': dps.abs(), // Para calibrar, la magnitud es suficiente
            });
          }
        },
        onError: (e) {
          print('[BLE] Error in IMU stream subscription: $e');
        },
      );
    } catch (e) {
      print('[BLE] Error subscribing to IMU stream: $e');
    }
  }
}

/// Estados de conexión BLE simplificados
enum BleConnectionState {
  disconnected,
  connecting,
  connected,
  disconnecting,
}
