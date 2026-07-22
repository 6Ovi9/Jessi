import 'dart:async';
import 'dart:convert';
import 'dart:collection';
import 'dart:io';
import 'dart:ui';
import 'dart:isolate';
import 'package:flutter/foundation.dart';
import 'package:flutter/widgets.dart';
import 'package:flutter_foreground_task/flutter_foreground_task.dart';
import 'package:shared_preferences/shared_preferences.dart';
import 'package:supabase/supabase.dart';
import 'package:flutter_dotenv/flutter_dotenv.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart' as fbp;
import 'package:geolocator/geolocator.dart';

class TelemetryItem {
  final Map<String, dynamic> data;
  TelemetryItem(this.data);
}

class BackgroundEngine {
  static const String _bleMacKey = 'ble_mac_address';
  static const String _accessTokenKey = 'supabase_access_token';
  static const String _refreshTokenKey = 'supabase_refresh_token';

  bool _initialized = false;
  final Queue<Map<String, dynamic>> _commandQueue = Queue();

  SupabaseClient? _supabase;
  SharedPreferences? _prefs;

  fbp.BluetoothDevice? _currentDevice;
  StreamSubscription<fbp.BluetoothConnectionState>? _bleConnectionSub;

  fbp.BluetoothCharacteristic? _batteryChar;
  fbp.BluetoothCharacteristic? _radarModeChar;
  fbp.BluetoothCharacteristic? _hapticTxChar;
  fbp.BluetoothCharacteristic? _hapticRxChar;
  fbp.BluetoothCharacteristic? _calibStatusChar;
  fbp.BluetoothCharacteristic? _calibThresholdChar;
  fbp.BluetoothCharacteristic? _imuStreamChar;
  fbp.BluetoothCharacteristic? _compassStreamChar;
  fbp.BluetoothCharacteristic? _distanceChar;
  fbp.BluetoothCharacteristic? _configChar;
  fbp.BluetoothCharacteristic? _calibCmdChar;
  fbp.BluetoothCharacteristic? _timeSyncChar;
  fbp.BluetoothCharacteristic? _bearingChar;
  String _bleState = 'disconnected';
  String? _currentMacAddress;

  StreamSubscription<Position>? _gpsSub;
  double _gpsLat = 0.0;
  double _gpsLng = 0.0;

  String _syncStatus = 'idle';
  String? _role;

  final Queue<TelemetryItem> _ringBuffer = Queue();
  static const int _maxRingBufferSize = 100;
  bool _isFlushingBuffer = false;

  StreamSubscription<AuthState>? _authSub;

  final SendPort? sendPort;

  bool _pausedForOta = false;
  int _batteryPercent = 100;
  bool _radarModeActive = false;
  Map<String, dynamic> _calibStatus = {};
  int _calibThreshold = 0;

  StreamSubscription<List<int>>? _batterySub;
  StreamSubscription<List<int>>? _radarModeSub;
  StreamSubscription<List<int>>? _hapticTxSub;
  StreamSubscription<List<int>>? _calibStatusSub;
  StreamSubscription<List<int>>? _calibThresholdSub;
  StreamSubscription<List<int>>? _imuStreamSub;
  StreamSubscription<List<int>>? _compassStreamSub;
  StreamSubscription<List<int>>? _configSub;

  DateTime _lastImuUpdate = DateTime.fromMillisecondsSinceEpoch(0);
  DateTime _lastCompassUpdate = DateTime.fromMillisecondsSinceEpoch(0);

  static final fbp.Guid _serviceUuid =
      fbp.Guid('4a5c180a-5f2d-4e1b-822c-4a2d87b4c85b');
  static final fbp.Guid _hapticRxCharUuid =
      fbp.Guid('4a5c2a5b-5f2d-4e1b-822c-4a2d87b4c85b');
  static final fbp.Guid _bearingCharUuid =
      fbp.Guid('4a5c2a58-5f2d-4e1b-822c-4a2d87b4c85b');
  static final fbp.Guid _distanceCharUuid =
      fbp.Guid('4a5c2a59-5f2d-4e1b-822c-4a2d87b4c85b');
  static final fbp.Guid _radarModeCharUuid =
      fbp.Guid('4a5c2a5f-5f2d-4e1b-822c-4a2d87b4c85b');
  static final fbp.Guid _configCharUuid =
      fbp.Guid('4a5c2a60-5f2d-4e1b-822c-4a2d87b4c85b');
  static final fbp.Guid _calibCmdCharUuid =
      fbp.Guid('4a5c2a5c-5f2d-4e1b-822c-4a2d87b4c85b');
  static final fbp.Guid _timeSyncCharUuid =
      fbp.Guid('4a5c2a2b-5f2d-4e1b-822c-4a2d87b4c85b');
  static final fbp.Guid _calibThresholdCharUuid =
      fbp.Guid('4a5c2a5e-5f2d-4e1b-822c-4a2d87b4c85b');

  static final fbp.Guid _batteryCharUuid =
      fbp.Guid('00002a19-0000-1000-8000-00805f9b34fb');
  static final fbp.Guid _hapticTxCharUuid =
      fbp.Guid('4a5c2a5a-5f2d-4e1b-822c-4a2d87b4c85b');
  static final fbp.Guid _calibStatusCharUuid =
      fbp.Guid('4a5c2a5d-5f2d-4e1b-822c-4a2d87b4c85b');
  static final fbp.Guid _imuStreamCharUuid =
      fbp.Guid('4a5c2a62-5f2d-4e1b-822c-4a2d87b4c85b');
  static final fbp.Guid _compassStreamCharUuid =
      fbp.Guid('4a5c2a64-5f2d-4e1b-822c-4a2d87b4c85b');

  BackgroundEngine({this.sendPort}) {
    PlatformDispatcher.instance.onError = (error, stack) {
      debugPrint('BackgroundEngine Global Error: $error\n$stack');
      return true;
    };
  }

  Future<void> start() async {
    WidgetsFlutterBinding.ensureInitialized();
    DartPluginRegistrant.ensureInitialized();

    _prefs = await SharedPreferences.getInstance();
    _role ??= _prefs!.getString('user_role');

    final accessToken = _prefs!.getString(_accessTokenKey);
    final refreshToken = _prefs!.getString(_refreshTokenKey);

    bool hasValidTokens = accessToken != null &&
        accessToken.isNotEmpty &&
        refreshToken != null &&
        refreshToken.isNotEmpty &&
        accessToken.split('.').length == 3;

    await dotenv.load(fileName: '.env');
    final supabaseUrl = dotenv.env['SUPABASE_URL']!;
    final supabaseKey = dotenv.env['SUPABASE_ANON_KEY']!;

    _supabase = SupabaseClient(supabaseUrl, supabaseKey);

    _authSub = _supabase!.auth.onAuthStateChange.listen((data) {
      final session = data.session;
      if (session != null) {
        _prefs!.setString(_accessTokenKey, session.accessToken);
        if (session.refreshToken != null) {
          _prefs!.setString(_refreshTokenKey, session.refreshToken!);
        }
        _sendToUi('token_update', {
          'access_token': session.accessToken,
          'refresh_token': session.refreshToken ?? '',
        });
      }
    });

    try {
      if (hasValidTokens) {
        await _supabase!.auth.setSession(refreshToken);
      } else {
        debugPrint(
            '[Background Engine] No stored session, signing in anonymously...');
        await _supabase!.auth.signInAnonymously();
      }
    } catch (e) {
      if (e is AuthException && e.statusCode == '401') {
        _handleSessionExpired();
      } else {
        debugPrint('[Background Engine] Auth error: $e');
      }
    }

    _currentMacAddress = _prefs!.getString(_bleMacKey);

    _startGpsPolling();

    if (_currentMacAddress != null) {
      _connectBle(_currentMacAddress!);
    }

    _initialized = true;
    _processCommandQueue();
  }

  void _startGpsPolling() async {
    try {
      final lastPos = await Geolocator.getLastKnownPosition();
      if (lastPos != null) {
        _gpsLat = lastPos.latitude;
        _gpsLng = lastPos.longitude;
        _sendToUi('gps_update', {
          'lat': _gpsLat,
          'lng': _gpsLng,
        });
        _syncTelemetry();
      }
    } catch (e) {
      debugPrint('[BG] getLastKnownPosition error: $e');
    }

    _gpsSub?.cancel();
    LocationSettings settings;
    if (Platform.isAndroid) {
      settings = AndroidSettings(
        accuracy: LocationAccuracy.high,
        distanceFilter: 0,
        intervalDuration: const Duration(seconds: 3),
      );
    } else {
      settings = const LocationSettings(
        accuracy: LocationAccuracy.high,
        distanceFilter: 0,
      );
    }

    _gpsSub = Geolocator.getPositionStream(locationSettings: settings)
        .listen((Position position) {
      _gpsLat = position.latitude;
      _gpsLng = position.longitude;
      _sendToUi('gps_update', {
        'lat': _gpsLat,
        'lng': _gpsLng,
      });
      _syncTelemetry();
    });
  }

  void onReceiveData(dynamic data) {
    if (data is! String) return;
    try {
      final map = jsonDecode(data) as Map<String, dynamic>;
      if (!_initialized) {
        _commandQueue.add(map);
      } else {
        _handleCommand(map);
      }
    } catch (e) {
      debugPrint('Invalid command payload: $e');
    }
  }

  void _processCommandQueue() {
    while (_commandQueue.isNotEmpty) {
      final cmd = _commandQueue.removeFirst();
      _handleCommand(cmd);
    }
  }

  void _handleCommand(Map<String, dynamic> cmd) {
    final type = cmd['type'] as String?;
    final payload = cmd['payload'] as Map<String, dynamic>? ?? {};

    switch (type) {
      case 'request_full_state':
        _sendFullState();
        break;
      case 'connect_ble':
        final mac = payload['mac_address'] as String?;
        if (mac != null) {
          _currentMacAddress = mac;
          _prefs!.setString(_bleMacKey, mac);
          _connectBle(mac);
        }
        break;
      case 'update_role':
        _role = payload['role'] as String?;
        break;
      case 'write_haptic':
        final pattern = payload['pattern'] as String?;
        if (pattern != null && _currentMacAddress != null) {
          _hapticRxChar?.write([0x01], withoutResponse: false).catchError((e) {
            debugPrint('write_haptic error: $e');
          });
        }
        break;
      case 'start_scan':
        // Scanning is now handled exclusively by the UI isolate to avoid
        // Android background scanning limitations.
        break;
      case 'stop_scan':
        // No-op
        break;
      case 'write_bearing':
        if (_currentMacAddress == null) break;
        final bearing = (payload['bearing'] as num?)?.toDouble() ?? 0.0;
        final bearingBytes = ByteData(4)..setFloat32(0, bearing, Endian.little);
        _bearingChar
            ?.write(bearingBytes.buffer.asUint8List(), withoutResponse: false)
            .catchError((_) {});
        break;
      case 'write_distance':
        if (_currentMacAddress == null) break;
        final distance = (payload['distance'] as num?)?.toInt() ?? 0;
        final distanceBytes = ByteData(4)
          ..setUint32(0, distance, Endian.little);
        _distanceChar
            ?.write(distanceBytes.buffer.asUint8List(), withoutResponse: false)
            .catchError((_) {});
        break;
      case 'write_radar_mode':
        if (_currentMacAddress == null) break;
        final active = payload['active'] as bool? ?? false;
        _radarModeChar?.write([active ? 0x01 : 0x00],
            withoutResponse: false).catchError((_) {});
        break;
      case 'write_config':
        if (_currentMacAddress == null) break;
        final configJson = payload['config_json'] as String? ?? '{}';
        Future.microtask(() => _writeConfigInChunks(configJson));
        break;
      case 'send_calib_cmd':
        if (_currentMacAddress == null) break;
        final cmdVal = payload['cmd'] as int? ?? 0;
        _calibCmdChar
            ?.write([cmdVal], withoutResponse: false).catchError((_) {});
        break;
      case 'sync_time':
        if (_currentMacAddress == null) break;
        final now = DateTime.now();
        final int nowEpoch = now.millisecondsSinceEpoch ~/ 1000;
        final int tzOffset = now.timeZoneOffset.inSeconds;
        final syncBytes = ByteData(8)
          ..setUint32(0, nowEpoch, Endian.little)
          ..setInt32(4, tzOffset, Endian.little);
        _timeSyncChar
            ?.write(syncBytes.buffer.asUint8List(), withoutResponse: false)
            .catchError((_) {});
        break;
      case 'write_wake_threshold':
        if (_currentMacAddress == null) break;
        final threshold = payload['threshold'] as int? ?? 0;
        _calibThresholdChar
            ?.write([threshold], withoutResponse: false).catchError((_) {});
        break;
      case 'pause_for_ota':
        _pausedForOta = true;
        _bleConnectionSub?.cancel();
        _cancelAllBleSubscriptions();
        _bleState = 'disconnected';
        _sendToUi('ota_ready', {});
        break;
      case 'resume_after_ota':
        _pausedForOta = false;
        if (_currentMacAddress != null) {
          _connectBle(_currentMacAddress!);
        }
        break;
      case 'stop_engine':
        _stopEngine();
        break;
    }
  }

  void _sendFullState() {
    _sendToUi('fullState', {
      'bleState': _bleState,
      'macAddress': _currentMacAddress ?? '',
      'gpsLat': _gpsLat,
      'gpsLng': _gpsLng,
      'syncStatus': _syncStatus,
      'batteryPercent': _batteryPercent,
      'radarModeActive': _radarModeActive,
      'calibStatus': _calibStatus,
      'calibThreshold': _calibThreshold,
      'paused_for_ota': _pausedForOta,
    });
  }

  void _connectBle(String macAddress) async {
    if (_pausedForOta) return;
    _bleConnectionSub?.cancel();

    // Safely stop any active scan first to free the Bluetooth radio (prevents scan_is_already_in_progress deadlocks)
    try {
      if (fbp.FlutterBluePlus.isScanningNow) {
        await fbp.FlutterBluePlus.stopScan();
        await Future.delayed(const Duration(milliseconds: 100));
      }
    } catch (_) {}

    try {
      await _currentDevice?.disconnect();
    } catch (_) {}
    await Future.delayed(const Duration(milliseconds: 200));

    _currentDevice = fbp.BluetoothDevice.fromId(macAddress);
    _bleState = 'connecting';
    _sendToUi('ble_update', {
      'status': _bleState,
      'device': macAddress,
    });

    _bleConnectionSub = _currentDevice!.connectionState.listen((state) {
      _bleState = state.name;
      _sendToUi('ble_update', {
        'status': _bleState,
        'device': _currentDevice!.remoteId.str,
      });

      if (state == fbp.BluetoothConnectionState.connected) {
        _autoReconnectTimer?.cancel();
        _subscribeToCharacteristics(_currentDevice!.remoteId.str).catchError((e) async {
          debugPrint('Subscribe/discovery error: $e');
          _bleState = 'disconnected';
          _sendToUi('ble_update', {
            'status': _bleState,
            'device': _currentDevice!.remoteId.str,
            'error': 'subscribe_failed:${e.toString()}',
          });
          try {
            await _currentDevice?.disconnect();
          } catch (_) {}
          _scheduleAutoReconnect();
        });
      } else if (state == fbp.BluetoothConnectionState.disconnected) {
        _cancelAllBleSubscriptions();
        _scheduleAutoReconnect();
      } else {
        _cancelAllBleSubscriptions();
      }
    });

    try {
      await _currentDevice!.connect(
        autoConnect: false,
        timeout: const Duration(seconds: 10),
        license: fbp.License.nonprofit,
      );
    } catch (e) {
      debugPrint('Direct connect error to $macAddress: $e');
      _bleState = 'disconnected';
      _sendToUi('ble_update', {
        'status': _bleState,
        'device': macAddress,
        'error': e.toString(),
      });
      _scheduleAutoReconnect();
    }
  }

  Timer? _autoReconnectTimer;

  void _scheduleAutoReconnect() {
    _autoReconnectTimer?.cancel();
    if (_currentMacAddress == null || _pausedForOta) return;
    _autoReconnectTimer = Timer(const Duration(seconds: 5), () {
      if (_bleState == 'disconnected' && _currentMacAddress != null && !_pausedForOta) {
        debugPrint('[BG] Auto-reconnecting to $_currentMacAddress...');
        _connectBle(_currentMacAddress!);
      }
    });
  }

  void _cancelAllBleSubscriptions() {
    _batterySub?.cancel();
    _radarModeSub?.cancel();
    _hapticTxSub?.cancel();
    _calibStatusSub?.cancel();
    _calibThresholdSub?.cancel();
    _imuStreamSub?.cancel();
    _compassStreamSub?.cancel();
    _configSub?.cancel();
  }

  Future<void> _safeWriteChar(fbp.BluetoothCharacteristic? char, List<int> value) async {
    if (char == null) return;
    try {
      await char.write(value, withoutResponse: false);
    } catch (_) {
      try {
        await char.write(value, withoutResponse: true);
      } catch (e) {
        debugPrint('[BG] Safe GATT write failed for ${char.uuid}: $e');
      }
    }
  }

  Future<void> _sendAutoSyncData() async {
    if (_currentMacAddress == null) return;
    debugPrint('[BG] Kicking off connection auto-sync for watch...');

    // 1. Sync Time
    if (_timeSyncChar != null) {
      try {
        final now = DateTime.now();
        final int nowEpoch = now.millisecondsSinceEpoch ~/ 1000;
        final int tzOffset = now.timeZoneOffset.inSeconds;
        final syncBytes = ByteData(8)
          ..setUint32(0, nowEpoch, Endian.little)
          ..setInt32(4, tzOffset, Endian.little);

        await _safeWriteChar(_timeSyncChar, syncBytes.buffer.asUint8List());
        debugPrint('[BG] Auto-synced time: Epoch $nowEpoch, offset $tzOffset');
      } catch (e) {
        debugPrint('[BG] Auto-sync time failed: $e');
      }
    }

    // 2. Sync Config (from cache)
    if (_configChar != null) {
      final configBleJson = _prefs?.getString('cached_watch_config_ble');
      if (configBleJson != null) {
        await _writeConfigInChunks(configBleJson);
        debugPrint('[BG] Auto-synced config in chunks');
      } else {
        debugPrint(
            '[BG] Auto-sync config skipped: no cached config in SharedPreferences');
      }
    }
  }

  Future<void> _writeConfigInChunks(String configJson) async {
    if (_configChar == null) return;
    try {
      final Map<String, dynamic> fullConfig = jsonDecode(configJson);

      // Split config into 5 logical smaller chunks (each < 120 bytes) to fit comfortably within MTU constraints
      final List<Map<String, dynamic>> chunks = [
        // Chunk 1: Clock & General Settings
        {
          if (fullConfig.containsKey('ct')) 'ct': fullConfig['ct'],
          if (fullConfig.containsKey('st')) 'st': fullConfig['st'],
          if (fullConfig.containsKey('br')) 'br': fullConfig['br'],
          if (fullConfig.containsKey('lb')) 'lb': fullConfig['lb'],
          if (fullConfig.containsKey('lg')) 'lg': fullConfig['lg'],
          if (fullConfig.containsKey('hp')) 'hp': fullConfig['hp'],
        },
        // Chunk 2: Clock Hands Colors
        {
          if (fullConfig.containsKey('chc')) 'chc': fullConfig['chc'],
          if (fullConfig.containsKey('cmc')) 'cmc': fullConfig['cmc'],
          if (fullConfig.containsKey('csc')) 'csc': fullConfig['csc'],
          if (fullConfig.containsKey('chd')) 'chd': fullConfig['chd'],
          if (fullConfig.containsKey('cmd')) 'cmd': fullConfig['cmd'],
          if (fullConfig.containsKey('csd')) 'csd': fullConfig['csd'],
        },
        // Chunk 3: Haptic & Flick Settings
        {
          if (fullConfig.containsKey('ctx')) 'ctx': fullConfig['ctx'],
          if (fullConfig.containsKey('crx')) 'crx': fullConfig['crx'],
          if (fullConfig.containsKey('btx')) 'btx': fullConfig['btx'],
          if (fullConfig.containsKey('brx')) 'brx': fullConfig['brx'],
          if (fullConfig.containsKey('wt')) 'wt': fullConfig['wt'],
          if (fullConfig.containsKey('gt')) 'gt': fullConfig['gt'],
          if (fullConfig.containsKey('df')) 'df': fullConfig['df'],
          if (fullConfig.containsKey('tf')) 'tf': fullConfig['tf'],
        },
        // Chunk 4: Radar & Distance Colors
        {
          if (fullConfig.containsKey('cra')) 'cra': fullConfig['cra'],
          if (fullConfig.containsKey('cdn')) 'cdn': fullConfig['cdn'],
          if (fullConfig.containsKey('cdp')) 'cdp': fullConfig['cdp'],
          if (fullConfig.containsKey('cdf')) 'cdf': fullConfig['cdf'],
          if (fullConfig.containsKey('cdv')) 'cdv': fullConfig['cdv'],
          if (fullConfig.containsKey('cde')) 'cde': fullConfig['cde'],
        },
        // Chunk 5: Distance Thresholds & LEDs
        {
          if (fullConfig.containsKey('dt1')) 'dt1': fullConfig['dt1'],
          if (fullConfig.containsKey('dt2')) 'dt2': fullConfig['dt2'],
          if (fullConfig.containsKey('dt3')) 'dt3': fullConfig['dt3'],
          if (fullConfig.containsKey('dt4')) 'dt4': fullConfig['dt4'],
          if (fullConfig.containsKey('dtm')) 'dtm': fullConfig['dtm'],
          if (fullConfig.containsKey('ln')) 'ln': fullConfig['ln'],
          if (fullConfig.containsKey('lp')) 'lp': fullConfig['lp'],
          if (fullConfig.containsKey('lf')) 'lf': fullConfig['lf'],
          if (fullConfig.containsKey('lv')) 'lv': fullConfig['lv'],
          if (fullConfig.containsKey('le')) 'le': fullConfig['le'],
        },
      ];

      for (final chunk in chunks) {
        if (chunk.isEmpty) continue;
        final chunkJson = jsonEncode(chunk);
        try {
          await _safeWriteChar(_configChar, utf8.encode(chunkJson));
          debugPrint('[BG] Successfully wrote config chunk: $chunkJson');
          await Future.delayed(const Duration(milliseconds: 150));
        } catch (e) {
          debugPrint(
              '[BG] Failed to write config chunk: $chunkJson, error: $e');
        }
      }
    } catch (e) {
      debugPrint('[BG] Error splitting and writing config chunks: $e');
    }
  }

  Future<void> _subscribeToCharacteristics(String macAddress) async {
    if (_currentDevice == null) return;
    _cancelAllBleSubscriptions();

    // Allow connection to settle before MTU/service requests
    await Future.delayed(const Duration(milliseconds: 500));

    if (Platform.isAndroid) {
      try {
        await _currentDevice!
            .requestMtu(223)
            .timeout(const Duration(seconds: 3));
        debugPrint('[BG] MTU negotiated successfully to 223');
      } catch (e) {
        debugPrint('[BG] Failed to negotiate MTU or timed out: $e');
      }
    }

    final services = await _currentDevice!.discoverServices();
    fbp.BluetoothService? targetService;
    for (final s in services) {
      if (s.uuid == _serviceUuid) {
        targetService = s;
        break;
      }
    }

    if (targetService == null) {
      debugPrint('Target BLE service not found after connect. Disconnecting.');
      _bleState = 'disconnected';
      _sendToUi('ble_update', {
        'status': _bleState,
        'device': macAddress,
        'error': 'target_service_not_found',
      });
      try {
        await _currentDevice!.disconnect();
      } catch (_) {}
      return;
    }

    for (final c in targetService.characteristics) {
      if (c.uuid == _batteryCharUuid) _batteryChar = c;
      if (c.uuid == _radarModeCharUuid) _radarModeChar = c;
      if (c.uuid == _hapticTxCharUuid) _hapticTxChar = c;
      if (c.uuid == _hapticRxCharUuid) _hapticRxChar = c;
      if (c.uuid == _calibStatusCharUuid) _calibStatusChar = c;
      if (c.uuid == _calibThresholdCharUuid) _calibThresholdChar = c;
      if (c.uuid == _imuStreamCharUuid) _imuStreamChar = c;
      if (c.uuid == _compassStreamCharUuid) _compassStreamChar = c;
      if (c.uuid == _distanceCharUuid) _distanceChar = c;
      if (c.uuid == _configCharUuid) _configChar = c;
      if (c.uuid == _calibCmdCharUuid) _calibCmdChar = c;
      if (c.uuid == _timeSyncCharUuid) _timeSyncChar = c;
      if (c.uuid == _bearingCharUuid) _bearingChar = c;
    }

    Future<void> safeSetNotify(
        fbp.BluetoothCharacteristic? char, Function(List<int>) onData) async {
      if (char == null) return;
      try {
        await char.setNotifyValue(true);
        await Future.delayed(const Duration(milliseconds: 50));
      } catch (e) {
        debugPrint('[BG] Non-critical setNotifyValue error for ${char.uuid}: $e');
      }
      char.onValueReceived.listen((data) => onData(data));
    }

    _bleState = 'connected';
    _sendToUi('ble_update', {
      'status': 'connected',
      'device': macAddress,
    });

    await safeSetNotify(_batteryChar, (data) {
      if (data.isNotEmpty) {
        _batteryPercent = data[0];
        _sendToUi('battery_update', {'percent': _batteryPercent});
      }
    });

    await safeSetNotify(_radarModeChar, (data) {
      if (data.isNotEmpty) {
        _radarModeActive = data[0] == 1;
        _sendToUi('radar_mode_update', {'active': _radarModeActive});
      }
    });

    await safeSetNotify(_hapticTxChar, (data) async {
      if (data.isNotEmpty && data[0] == 0x01) {
        _sendToUi('haptic_tx_received', {});
        final myId = _role ?? 'A';
        final partnerId = myId == 'A' ? 'B' : 'A';
        try {
          if (_supabase != null) {
            await _supabase!.from('haptic_events').insert({
              'from_user': myId,
              'to_user': partnerId,
            });
            debugPrint('[BG] Direct touch event inserted to Supabase ($myId -> $partnerId)');
          }
        } catch (e) {
          debugPrint('[BG] Error inserting direct touch event: $e');
        }
      }
    });

    await safeSetNotify(_calibStatusChar, (data) {
      if (data.isNotEmpty) {
        _calibStatus = {'status': data[0]};
        _sendToUi('calib_status_update', {'status': _calibStatus});
      }
    });

    await safeSetNotify(_calibThresholdChar, (data) {
      if (data.isNotEmpty) {
        _calibThreshold = data[0];
        _sendToUi('calib_threshold_update', {'threshold': _calibThreshold});
      }
    });

    await safeSetNotify(_imuStreamChar, (data) {
      if (data.length >= 6) {
        final now = DateTime.now();
        if (now.difference(_lastImuUpdate).inMilliseconds >= 500) {
          _lastImuUpdate = now;
          final bytes = ByteData.sublistView(Uint8List.fromList(data));
          _sendToUi('imu_stream_update', {
            'data': {
              'x': bytes.getInt16(0, Endian.little),
              'y': bytes.getInt16(2, Endian.little),
              'z': bytes.getInt16(4, Endian.little),
            }
          });
        }
      }
    });

    await safeSetNotify(_compassStreamChar, (data) {
      if (data.length >= 4) {
        final now = DateTime.now();
        if (now.difference(_lastCompassUpdate).inMilliseconds >= 500) {
          _lastCompassUpdate = now;
          final bytes = ByteData.sublistView(Uint8List.fromList(data));
          _sendToUi('compass_stream_update', {
            'heading': bytes.getFloat32(0, Endian.little),
          });
        }
      }
    });

    await safeSetNotify(_configChar, (data) {
      if (data.isNotEmpty) {
        final text = utf8.decode(data, allowMalformed: true);
        debugPrint('[BG] Watch confirmed config chunk received: $text');
        _sendToUi('config_confirmed', {'config': text});
      }
    });

    try {
      await _sendAutoSyncData();
    } catch (e) {
      debugPrint('[BG] Non-critical auto-sync error: $e');
    }
  }

  Future<void> _syncTelemetry() async {
    if (_supabase == null) return;

    final payload = {
      'lat': _gpsLat,
      'lng': _gpsLng,
      'role': _role,
      'timestamp': DateTime.now().toIso8601String(),
    };

    _ringBuffer.add(TelemetryItem(payload));
    if (_ringBuffer.length > _maxRingBufferSize) {
      _ringBuffer.removeFirst();
    }

    _flushTelemetryBuffer();
  }

  Future<void> _flushTelemetryBuffer() async {
    if (_isFlushingBuffer || _ringBuffer.isEmpty) return;
    _isFlushingBuffer = true;
    _syncStatus = 'syncing';
    _sendFullState();

    while (_ringBuffer.isNotEmpty) {
      final item = _ringBuffer.first;
      try {
        await _supabase!.from('telemetry').insert(item.data);
        _ringBuffer.removeFirst();
        await Future.delayed(
            const Duration(seconds: 1)); // Throttled polling interval
      } on SocketException catch (_) {
        _syncStatus = 'offline';
        _sendToUi('syncError', {
          'message': 'Network offline, buffering telemetry',
          'code': 'offline'
        });
        _sendFullState();
        break;
      } on AuthException catch (e) {
        if (e.statusCode == '401') {
          _syncStatus = 'error';
          _handleSessionExpired();
          return;
        } else {
          _syncStatus = 'error';
          _sendToUi('syncError', {
            'message': e.message,
            'code': e.statusCode ?? 'unknown',
          });
          _sendFullState();
        }
        break;
      } catch (e) {
        _syncStatus = 'error';
        _sendToUi('syncError', {
          'message': e.toString(),
          'code': 'unknown',
        });
        _sendFullState();
        break;
      }
    }

    if (_syncStatus == 'syncing') {
      _syncStatus = 'idle';
      _sendFullState();
    }
    _isFlushingBuffer = false;
  }

  void _handleSessionExpired() {
    _prefs?.remove(_accessTokenKey);
    _prefs?.remove(_refreshTokenKey);
    _sendToUi('session_expired', {});
    _stopEngine();
  }

  Future<void> _stopEngine() async {
    _authSub?.cancel();
    _gpsSub?.cancel();
    _bleConnectionSub?.cancel();
    _cancelAllBleSubscriptions();

    // Wait for any pending messages to flush over the port
    await Future.delayed(const Duration(milliseconds: 500));
    await FlutterForegroundTask.stopService();
  }

  void _sendToUi(String type, Map<String, dynamic> payload) {
    final msg = jsonEncode({'type': type, 'payload': payload});
    sendPort?.send(msg);
  }
}
