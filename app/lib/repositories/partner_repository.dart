import 'package:flutter/foundation.dart';
import 'package:supabase_flutter/supabase_flutter.dart';

import '../models/config_model.dart';

/// Repositorio para gestionar datos de la pareja y configuración.
///
/// Centraliza las operaciones de lectura/escritura en Supabase
/// para la configuración del reloj y el estado del emparejamiento.
class PartnerRepository extends ChangeNotifier {
  late SupabaseClient _client;
  bool _isInitialized = false;

  // ── Estado ──────────────────────────────────────────────────────────────

  /// ID del usuario actual ("A" o "B")
  String _myUserId = 'A';
  String get myUserId => _myUserId;

  /// ID de la pareja
  String get partnerUserId => _myUserId == 'A' ? 'B' : 'A';

  /// Configuración actual del reloj
  WatchConfig? _config;
  WatchConfig? get config => _config;

  /// ¿Está emparejado? (es decir, ¿hay un partner configurado?)
  bool _isPaired = false;
  bool get isPaired => _isPaired;

  /// ID del dispositivo BLE guardado (para reconexión automática)
  String? _savedDeviceId;
  String? get savedDeviceId => _savedDeviceId;

  // ── Lifecycle ──────────────────────────────────────────────────────────

  /// Inicializar el repositorio (idempotente — seguro llamar varias veces)
  Future<void> initialize(String userId) async {
    _myUserId = userId;
    _client = Supabase.instance.client;

    // Cargar configuración
    await loadConfig();

    // Verificar emparejamiento
    _isPaired = true; // En este sistema de 2 usuarios, siempre están emparejados
    _isInitialized = true;

    print('[REPO] Initialized for user: $_myUserId');
    notifyListeners();
  }

  // ── Configuración del reloj ────────────────────────────────────────────

  /// Cargar la configuración desde Supabase
  Future<WatchConfig> loadConfig() async {
    try {
      final data = await _client
          .from('watch_config')
          .select()
          .eq('user_id', _myUserId)
          .single()
          .timeout(const Duration(seconds: 5));

      _config = WatchConfig.fromJson(data);
      print('[REPO] Config loaded: brightness=${_config!.brightnessPercent}%');
      notifyListeners();
      return _config!;
    } catch (e) {
      print('[REPO] Error loading config, using defaults: $e');
      _config = WatchConfig.defaultFor(_myUserId);
      notifyListeners();
      return _config!;
    }
  }

  /// Guardar la configuración en Supabase
  Future<void> saveConfig(WatchConfig config) async {
    try {
      _config = config;
      await _client.from('watch_config').upsert(config.toJson());
      print('[REPO] Config saved');
      notifyListeners();
    } catch (e) {
      print('[REPO] Error saving config: $e');
    }
  }

  /// Actualizar un campo específico de la configuración
  Future<void> updateConfig(WatchConfig Function(WatchConfig current) updater) async {
    if (_config == null) await loadConfig();
    final updated = updater(_config!);
    await saveConfig(updated);
  }

  // ── Emparejamiento ────────────────────────────────────────────────────

  /// Guardar el ID del dispositivo BLE para reconexión automática
  void saveDeviceId(String deviceId) {
    _savedDeviceId = deviceId;
    _isPaired = true;
    notifyListeners();
    print('[REPO] Device ID saved: $deviceId');
  }

  /// Limpiar el emparejamiento
  void clearPairing() {
    _savedDeviceId = null;
    _isPaired = false;
    notifyListeners();
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

      if (response.user != null) {
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
    await _client.auth.signOut();
    print('[REPO] Signed out');
  }
}
