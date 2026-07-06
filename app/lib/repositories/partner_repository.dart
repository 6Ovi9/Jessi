import 'package:flutter/foundation.dart';
import 'package:shared_preferences/shared_preferences.dart';
import 'package:supabase_flutter/supabase_flutter.dart';

import '../models/config_model.dart';

/// Repositorio para gestionar datos de la pareja y configuración.
///
/// Centraliza las operaciones de lectura/escritura en Supabase
/// para la configuración del reloj y el estado del emparejamiento.
class PartnerRepository extends ChangeNotifier {
  late SupabaseClient _client;
  bool _isInitialized = false;
  bool _disposed = false;

  @override
  void dispose() {
    _disposed = true;
    super.dispose();
  }

  // ── Estado ──────────────────────────────────────────────────────────────

  /// ID del usuario actual ("A" o "B")
  String _myUserId = '';
  String get myUserId => _myUserId;

  /// ID de la pareja
  String get partnerUserId => _myUserId == 'A' ? 'B' : 'A';

  /// Configuración actual del reloj
  WatchConfig? _config;
  WatchConfig? get config => _config;

  int _sessionId = 0;
  int _pendingRequests = 0;
  int _requestNonce = 0;
  int _lastSuccessfulNonce = 0;
  WatchConfig? _lastConfirmedServerConfig;

  /// ¿Está emparejado? (es decir, ¿hay un partner configurado?)
  bool _isPaired = false;
  bool get isPaired => _isPaired;

  /// ID del dispositivo BLE guardado (para reconexión automática)
  String? _savedDeviceId;
  String? get savedDeviceId => _savedDeviceId;

  // ── Lifecycle ──────────────────────────────────────────────────────────

  /// Inicializar el repositorio (idempotente — seguro llamar varias veces)
  Future<void> initialize(String userId) async {
    if (_isInitialized && _myUserId == userId) return;
    if (_config != null && _config!.userId != userId) {
      _config = null;
      _lastConfirmedServerConfig = null;
    }
    _myUserId = userId;
    _client = Supabase.instance.client;

    _sessionId++;
    final currentSessionId = _sessionId;
    _pendingRequests = 0;

    // Cargar configuración
    await loadConfig(currentSessionId);
    
    if (_sessionId != currentSessionId) return;

    // Verificar emparejamiento
    try {
      final prefs = await SharedPreferences.getInstance();
      _savedDeviceId = prefs.getString('ble_device_id');
      _isPaired = _savedDeviceId != null;
    } catch (_) {
      _isPaired = false;
    }
    _lastSuccessfulNonce = 0;
    _isInitialized = true;

    print('[REPO] Initialized for user: $_myUserId');
    if (!_disposed) notifyListeners();
  }

  // ── Configuración del reloj ────────────────────────────────────────────

  /// Cargar la configuración desde Supabase
  Future<WatchConfig> loadConfig([int? currentSessionId]) async {
    final targetSession = currentSessionId ?? _sessionId;
    try {
      final data = await _client
          .from('watch_config')
          .select()
          .eq('user_id', _myUserId)
          .single()
          .timeout(const Duration(seconds: 5));

      if (_sessionId != targetSession) return WatchConfig.defaultFor(_myUserId);

      if (_pendingRequests > 0) {
        return _config!;
      }

      _config = WatchConfig.fromJson(data);
      _lastConfirmedServerConfig = _config;
      print('[REPO] Config loaded: brightness=${_config!.brightnessPercent}%');
      if (!_disposed) notifyListeners();
      return _config!;
    } on PostgrestException catch (e) {
      if (_sessionId != targetSession) return WatchConfig.defaultFor(_myUserId);
      if (e.code == 'PGRST116') { // Record not found
        if (_pendingRequests == 0) {
          _config = WatchConfig.defaultFor(_myUserId);
          _lastConfirmedServerConfig = _config;
          if (!_disposed) notifyListeners();
        }
        return _config ?? WatchConfig.defaultFor(_myUserId);
      }
      // Fallthrough for other DB errors
      print('[REPO] Error loading config: $e');
      if (_config == null) {
        _config = WatchConfig.defaultFor(_myUserId);
        _lastConfirmedServerConfig = _config;
        if (!_disposed) notifyListeners();
      }
      return _config!;
    } catch (e) {
      // Network or other exceptions - KEEP existing config if offline!
      if (_sessionId != targetSession) return WatchConfig.defaultFor(_myUserId);
      print('[REPO] Error loading config: $e');
      if (_config == null) {
        _config = WatchConfig.defaultFor(_myUserId);
        _lastConfirmedServerConfig = _config;
        if (!_disposed) notifyListeners();
      }
      return _config!;
    }
  }

  /// Guardar la configuración en Supabase
  Future<void> saveConfig(WatchConfig config) async {
    if (_myUserId.isEmpty) throw Exception('Repository not initialized');
    final currentSessionId = _sessionId;
    final currentNonce = ++_requestNonce;
    _pendingRequests++;
    
    _config = config;
    if (!_disposed) notifyListeners();
    
    try {
      await _client.from('watch_config').upsert(config.toJson()).timeout(const Duration(seconds: 10));
      if (_sessionId == currentSessionId && currentNonce > _lastSuccessfulNonce) {
        _lastSuccessfulNonce = currentNonce;
        _lastConfirmedServerConfig = config;
      }
      print('[REPO] Config saved');
    } catch (e) {
      print('[REPO] Error saving config: $e');
      rethrow;
    } finally {
      if (_sessionId == currentSessionId) {
        _pendingRequests--;
        if (_pendingRequests == 0 && _config != _lastConfirmedServerConfig) {
           _config = _lastConfirmedServerConfig;
           if (!_disposed) notifyListeners();
        }
      }
    }
  }

  /// Actualizar un campo específico de la configuración
  Future<void> updateConfig(WatchConfig Function(WatchConfig current) updater) async {
    if (_config == null) await loadConfig();
    if (_config == null) return;
    _config = updater(_config!);
    await saveConfig(_config!);
  }

  // ── Emparejamiento ────────────────────────────────────────────────────

  /// Guardar el ID del dispositivo BLE para reconexión automática
  Future<void> saveDeviceId(String deviceId) async {
    _savedDeviceId = deviceId;
    _isPaired = true;
    
    try {
      final prefs = await SharedPreferences.getInstance();
      if (!_isPaired) return;
      await prefs.setString('ble_device_id', deviceId);
    } catch (e) {
      print('[REPO] Error saving device ID: $e');
    }

    if (!_disposed) notifyListeners();
    print('[REPO] Device ID saved: $deviceId');
  }

  /// Limpiar el emparejamiento
  Future<void> clearPairing() async {
    _savedDeviceId = null;
    _isPaired = false;
    
    try {
      final prefs = await SharedPreferences.getInstance();
      await prefs.remove('ble_device_id');
    } catch (e) {
      print('[REPO] Error clearing pairing: $e');
    }

    if (!_disposed) notifyListeners();
    print('[REPO] Pairing cleared');
  }

  // ── Autenticación simple ──────────────────────────────────────────────

  /// Login simple con email y password.
  ///
  /// En este sistema de 2 usuarios, cada uno tiene su email:
  /// - Usuario A: [email configurado]
  /// - Usuario B: [email configurado]
  Future<bool> signIn(String email, String password) async {
    try {
      final response = await _client.auth.signInWithPassword(
        email: email,
        password: password,
      );

      if (response.session != null) {
        print('[REPO] Signed in as: $email');
        return true;
      }
      return false;
    } catch (e) {
      print('[REPO] Sign in error: $e');
      return false;
    }
  }

  /// Registrar un nuevo usuario
  Future<bool> signUp(String email, String password) async {
    try {
      final response = await _client.auth.signUp(
        email: email,
        password: password,
      );

      if (response.session != null) {
        print('[REPO] Signed up as: $email');
        return true;
      }
      return false;
    } catch (e) {
      print('[REPO] Sign up error: $e');
      return false;
    }
  }

  /// ¿Hay una sesión activa?
  bool get isAuthenticated => _client.auth.currentSession != null;

  /// Cerrar sesión
  Future<void> signOut() async {
    try {
      await _client.auth.signOut();
      print('[REPO] Signed out');
    } catch (e) {
      print('[REPO] Error signing out: $e');
    }
  }
}
