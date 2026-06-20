import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:geolocator/geolocator.dart';
import 'package:latlong2/latlong.dart';
import 'package:supabase_flutter/supabase_flutter.dart';

import '../models/location_model.dart';

/// Servicio de sincronización con Supabase.
///
/// Gestiona:
/// - UPSERT de la ubicación propia en la tabla `locations`
/// - Suscripción Realtime a la ubicación de la pareja
/// - Suscripción Realtime a eventos hápticos
/// - Envío de eventos hápticos
class SyncService extends ChangeNotifier {
  late SupabaseClient _client;

  // ── Estado ──────────────────────────────────────────────────────────────

  /// ID del usuario actual ("A" o "B")
  String _myUserId = 'A';
  String get myUserId => _myUserId;

  /// ID de la pareja ("B" o "A")
  String get partnerUserId => _myUserId == 'A' ? 'B' : 'A';

  /// Última ubicación conocida de la pareja
  LocationModel? _partnerLocation;
  LocationModel? get partnerLocation => _partnerLocation;

  /// ¿Está conectado a Supabase?
  bool _isConnected = false;
  bool get isConnected => _isConnected;

  // ── Subscripciones ─────────────────────────────────────────────────────

  RealtimeChannel? _locationChannel;
  RealtimeChannel? _hapticChannel;

  // ── Callbacks ──────────────────────────────────────────────────────────

  /// Callback cuando se recibe una nueva ubicación de la pareja
  ValueChanged<LatLng>? onPartnerLocationUpdate;

  /// Callback cuando se recibe un evento háptico de la pareja
  VoidCallback? onHapticEventReceived;

  // ── Lifecycle ──────────────────────────────────────────────────────────

  /// Inicializar el servicio de sincronización.
  ///
  /// [userId] es "A" o "B" — identifica a este usuario en el sistema.
  Future<void> initialize(String userId) async {
    _myUserId = userId;
    _client = Supabase.instance.client;

    print('[SYNC] Initialized as user: $_myUserId (partner: $partnerUserId)');

    // Health check real: intentar consultar la tabla locations
    await checkConnection();

    // Iniciar subscripciones Realtime (incluso si el servidor no responde
    // ahora — se reconectarán automáticamente cuando esté disponible)
    _subscribeToPartnerLocation();
    _subscribeToHapticEvents();
  }

  /// Verificar la conexión con Supabase haciendo una consulta real.
  ///
  /// Actualiza [isConnected] según el resultado.
  Future<bool> checkConnection() async {
    try {
      await _client
          .from('locations')
          .select('user_id')
          .limit(1)
          .timeout(const Duration(seconds: 5));

      _updateConnectionStatus(true);
      print('[SYNC] Health check: OK — Supabase reachable');
      return true;
    } catch (e) {
      _updateConnectionStatus(false);
      print('[SYNC] Health check: FAILED — $e');
      return false;
    }
  }

  void _updateConnectionStatus(bool connected) {
    if (_isConnected != connected) {
      _isConnected = connected;
      notifyListeners();
    }
  }

  @override
  void dispose() {
    _locationChannel?.unsubscribe();
    _hapticChannel?.unsubscribe();
    super.dispose();
  }

  // ── Ubicación propia → Supabase ────────────────────────────────────────

  /// Subir la posición GPS actual a Supabase (UPSERT en tabla locations).
  ///
  /// [position] es el fix GPS actual.
  /// [pollingMode] es el modo de polling activo (para debugging en Supabase).
  Future<void> uploadLocation(Position position, String pollingMode) async {
    try {
      final location = LocationModel(
        userId: _myUserId,
        latitude: position.latitude,
        longitude: position.longitude,
        accuracy: position.accuracy,
        pollingMode: pollingMode,
      );

      await _client.from('locations').upsert(location.toJson());

      _updateConnectionStatus(true);
      print('[SYNC] Location uploaded: '
          '${position.latitude.toStringAsFixed(5)}, '
          '${position.longitude.toStringAsFixed(5)} [$pollingMode]');
    } catch (e) {
      _updateConnectionStatus(false);
      print('[SYNC] Error uploading location: $e');
    }
  }

  // ── Ubicación de la pareja ← Supabase Realtime ────────────────────────

  /// Suscribirse a cambios en la ubicación de la pareja via Realtime.
  void _subscribeToPartnerLocation() {
    _locationChannel = _client
        .channel('partner-location')
        .onPostgresChanges(
          event: PostgresChangeEvent.update,
          schema: 'public',
          table: 'locations',
          filter: PostgresChangeFilter(
            type: PostgresChangeFilterType.eq,
            column: 'user_id',
            value: partnerUserId,
          ),
          callback: (payload) {
            final newData = payload.newRecord;
            if (newData.isNotEmpty) {
              _partnerLocation = LocationModel.fromJson(newData);

              print('[SYNC] Partner location updated: '
                  '${_partnerLocation!.latitude.toStringAsFixed(5)}, '
                  '${_partnerLocation!.longitude.toStringAsFixed(5)}');

              onPartnerLocationUpdate?.call(LatLng(
                _partnerLocation!.latitude,
                _partnerLocation!.longitude,
              ));

              notifyListeners();
            }
          },
        )
        .subscribe();

    print('[SYNC] Subscribed to partner location (user: $partnerUserId)');
  }

  // ── Eventos hápticos ──────────────────────────────────────────────────

  /// Suscribirse a eventos hápticos dirigidos a este usuario.
  void _subscribeToHapticEvents() {
    _hapticChannel = _client
        .channel('haptic-events')
        .onPostgresChanges(
          event: PostgresChangeEvent.insert,
          schema: 'public',
          table: 'haptic_events',
          filter: PostgresChangeFilter(
            type: PostgresChangeFilterType.eq,
            column: 'to_user',
            value: _myUserId,
          ),
          callback: (payload) {
            final newData = payload.newRecord;
            if (newData.isNotEmpty) {
              final consumed = newData['consumed'] as bool? ?? false;
              if (!consumed) {
                final eventId = newData['id'] as String;
                print('[SYNC] Haptic event received! ID: $eventId');

                // Marcar como consumido
                _consumeHapticEvent(eventId);

                // Notificar
                onHapticEventReceived?.call();
              }
            }
          },
        )
        .subscribe();

    print('[SYNC] Subscribed to haptic events (user: $_myUserId)');
  }

  /// Enviar un evento háptico a la pareja.
  ///
  /// Se llama cuando el reloj notifica HAPTIC_TX (el usuario tocó el reloj).
  Future<void> sendHapticEvent() async {
    try {
      await _client.from('haptic_events').insert({
        'from_user': _myUserId,
        'to_user': partnerUserId,
      });

      print('[SYNC] Haptic event sent to partner $partnerUserId');
    } catch (e) {
      print('[SYNC] Error sending haptic event: $e');
    }
  }

  /// Marcar un evento háptico como consumido
  Future<void> _consumeHapticEvent(String eventId) async {
    try {
      await _client
          .from('haptic_events')
          .update({'consumed': true})
          .eq('id', eventId);

      print('[SYNC] Haptic event $eventId consumed');
    } catch (e) {
      print('[SYNC] Error consuming haptic event: $e');
    }
  }

  // ── Carga inicial ─────────────────────────────────────────────────────

  /// Obtener la última ubicación conocida de la pareja (sin Realtime).
  ///
  /// Útil en el arranque, antes de que llegue la primera actualización Realtime.
  Future<LocationModel?> fetchPartnerLocation() async {
    try {
      final data = await _client
          .from('locations')
          .select()
          .eq('user_id', partnerUserId)
          .single();

      final location = LocationModel.fromJson(data);

      if (location.isValid) {
        _partnerLocation = location;
        notifyListeners();
        print('[SYNC] Fetched partner location: '
            '${location.latitude.toStringAsFixed(5)}, '
            '${location.longitude.toStringAsFixed(5)} '
            '(age: ${location.age.inSeconds}s)');
        return location;
      }

      return null;
    } catch (e) {
      print('[SYNC] Error fetching partner location: $e');
      return null;
    }
  }

  /// Limpiar eventos hápticos antiguos consumidos
  Future<void> cleanupOldHapticEvents() async {
    try {
      await _client.rpc('cleanup_old_haptic_events');
      print('[SYNC] Old haptic events cleaned up');
    } catch (e) {
      print('[SYNC] Error cleaning up haptic events: $e');
    }
  }
}
