import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:geolocator/geolocator.dart';
import 'package:latlong2/latlong.dart';

import 'bearing_calculator.dart';

/// Servicio de localización GPS con Dynamic Polling.
///
/// Gestiona la obtención de coordenadas GPS en background con
/// intervalos dinámicos que escalan según la distancia a la pareja:
///
/// | Modo       | Condición                      | Intervalo |
/// |------------|-------------------------------|-----------|
/// | PRECISION  | <500m o RADAR activo          | 3s        |
/// | NEAR       | <10km                         | 60s       |
/// | FAR        | 10-50km                       | 3 min     |
/// | REMOTE     | >50km                         | 5-10 min  |
///
/// **Impacto**: 95-99% menos tráfico que polling estático 3s.
class LocationService extends ChangeNotifier {
  // ── Estado ──────────────────────────────────────────────────────────────

  /// Última posición GPS propia
  Position? _currentPosition;
  Position? get currentPosition => _currentPosition;

  /// Ubicación de la pareja (recibida de Supabase)
  LatLng? _partnerLocation;

  /// Modo de polling actual
  GpsPollingMode _currentMode = GpsPollingMode.near; // Default seguro (nota 8)
  GpsPollingMode get currentMode => _currentMode;

  /// ¿El reloj está en RADAR_MODE?
  bool _radarModeActive = false;
  bool get radarModeActive => _radarModeActive;

  /// Distancia actual a la pareja (km)
  double _distanceKm = -1;
  double get distanceKm => _distanceKm;

  /// Bearing actual hacia la pareja (grados)
  double _bearingDeg = 0;
  double get bearingDeg => _bearingDeg;

  /// ¿Servicio activo?
  bool _isRunning = false;
  bool get isRunning => _isRunning;

  /// Contador de actualizaciones GPS (para estadísticas)
  int _updateCount = 0;
  int get updateCount => _updateCount;

  // ── Timer de polling ───────────────────────────────────────────────────

  Timer? _pollingTimer;

  // ── Intervalos configurables ───────────────────────────────────────────

  int _precisionIntervalS = 3;
  int _nearIntervalS = 60;
  int _farIntervalS = 180;
  int _remoteMinIntervalS = 300;
  int _remoteMaxIntervalS = 600;

  // ── Callbacks ──────────────────────────────────────────────────────────

  /// Callback cuando se obtiene una nueva posición GPS
  /// Parámetros: (posición, bearing hacia pareja, distancia en metros)
  void Function(Position position, double bearing, int distanceM)? onLocationUpdate;

  // ── Lifecycle ──────────────────────────────────────────────────────────

  /// Verificar y solicitar permisos de ubicación
  Future<bool> checkAndRequestPermissions() async {
    bool serviceEnabled = await Geolocator.isLocationServiceEnabled();
    if (!serviceEnabled) {
      print('[GPS] Location services are disabled');
      return false;
    }

    LocationPermission permission = await Geolocator.checkPermission();

    if (permission == LocationPermission.denied) {
      permission = await Geolocator.requestPermission();
      if (permission == LocationPermission.denied) {
        print('[GPS] Location permission denied');
        return false;
      }
    }

    if (permission == LocationPermission.deniedForever) {
      print('[GPS] Location permission permanently denied');
      return false;
    }

    // Para background location en Android
    if (permission == LocationPermission.whileInUse) {
      // Necesitamos "always" para background
      // En Android 11+, primero se da "while in use", luego "always"
      permission = await Geolocator.requestPermission();
    }

    print('[GPS] Location permission granted: $permission');
    return true;
  }

  /// Iniciar el servicio de localización con dynamic polling.
  ///
  /// Arranca en modo NEAR (60s) como valor seguro por defecto,
  /// hasta recibir la primera ubicación de la pareja para calcular
  /// la distancia real. (Nota de implementación #8 de la spec)
  Future<void> start() async {
    if (_isRunning) return;

    final hasPermission = await checkAndRequestPermissions();
    if (!hasPermission) {
      print('[GPS] Cannot start: no permission');
      return;
    }

    _isRunning = true;
    _currentMode = GpsPollingMode.near; // Default seguro
    notifyListeners();

    print('[GPS] Service started in ${_currentMode.label} mode');

    // Primera actualización inmediata
    await _doGpsUpdate();

    // Programar siguiente actualización
    _scheduleNextUpdate();
  }

  /// Detener el servicio de localización
  void stop() {
    _pollingTimer?.cancel();
    _pollingTimer = null;
    _isRunning = false;
    notifyListeners();
    print('[GPS] Service stopped');
  }

  @override
  void dispose() {
    stop();
    super.dispose();
  }

  // ── Actualización de ubicación de la pareja ────────────────────────────

  /// Actualizar la ubicación de la pareja (recibida de Supabase Realtime).
  ///
  /// Recalcula bearing y distancia, y ajusta el modo de polling GPS.
  void updatePartnerLocation(LatLng partnerLocation) {
    _partnerLocation = partnerLocation;

    if (_currentPosition != null) {
      final myPos = LatLng(
        _currentPosition!.latitude,
        _currentPosition!.longitude,
      );

      _distanceKm = BearingCalculator.calculateDistanceKm(myPos, partnerLocation);
      _bearingDeg = BearingCalculator.calculateBearing(myPos, partnerLocation);

      // Recalcular modo de polling
      final newMode = BearingCalculator.getPollingMode(_distanceKm, _radarModeActive);
      if (newMode != _currentMode) {
        print('[GPS] Polling mode changed: ${_currentMode.label} → ${newMode.label}');
        _currentMode = newMode;
        _scheduleNextUpdate(); // Reprogramar con nuevo intervalo
      }

      notifyListeners();
    }
  }

  /// Notificar que el reloj entró/salió de RADAR_MODE.
  ///
  /// Si entra en RADAR, fuerza modo PRECISION inmediatamente.
  void setRadarModeActive(bool active) {
    if (_radarModeActive == active) return;

    _radarModeActive = active;
    print('[GPS] Radar mode: ${active ? "ON → PRECISION" : "OFF"}');

    if (active) {
      // Forzar modo PRECISION y actualización inmediata
      _currentMode = GpsPollingMode.precision;
      _doGpsUpdate();
      _scheduleNextUpdate();
    } else {
      // Recalcular modo según distancia actual
      if (_distanceKm >= 0) {
        _currentMode = BearingCalculator.getPollingMode(_distanceKm, false);
        _scheduleNextUpdate();
      }
    }

    notifyListeners();
  }

  /// Configurar intervalos de polling personalizados
  void setPollingIntervals({
    int? precisionS,
    int? nearS,
    int? farS,
    int? remoteMinS,
    int? remoteMaxS,
  }) {
    _precisionIntervalS = precisionS ?? _precisionIntervalS;
    _nearIntervalS = nearS ?? _nearIntervalS;
    _farIntervalS = farS ?? _farIntervalS;
    _remoteMinIntervalS = remoteMinS ?? _remoteMinIntervalS;
    _remoteMaxIntervalS = remoteMaxS ?? _remoteMaxIntervalS;
  }

  // ── Lógica interna ─────────────────────────────────────────────────────

  /// Programar la siguiente actualización GPS según el modo actual
  void _scheduleNextUpdate() {
    _pollingTimer?.cancel();

    final interval = BearingCalculator.getPollingInterval(
      _currentMode,
      precisionS: _precisionIntervalS,
      nearS: _nearIntervalS,
      farS: _farIntervalS,
      remoteMinS: _remoteMinIntervalS,
      remoteMaxS: _remoteMaxIntervalS,
    );

    _pollingTimer = Timer(interval, () async {
      if (_isRunning) {
        await _doGpsUpdate();
        _scheduleNextUpdate(); // Reprogramar el siguiente ciclo
      }
    });

    print('[GPS] Next update in ${interval.inSeconds}s (${_currentMode.label})');
  }

  /// Obtener posición GPS actual y notificar
  Future<void> _doGpsUpdate() async {
    try {
      final position = await Geolocator.getCurrentPosition(
        desiredAccuracy: _currentMode == GpsPollingMode.precision
            ? LocationAccuracy.bestForNavigation
            : LocationAccuracy.high,
        timeLimit: const Duration(seconds: 10),
      );

      _currentPosition = position;
      _updateCount++;

      // Recalcular bearing y distancia si tenemos ubicación de la pareja
      if (_partnerLocation != null) {
        final myPos = LatLng(position.latitude, position.longitude);
        _distanceKm = BearingCalculator.calculateDistanceKm(myPos, _partnerLocation!);
        _bearingDeg = BearingCalculator.calculateBearing(myPos, _partnerLocation!);

        // Notificar con datos calculados
        onLocationUpdate?.call(
          position,
          _bearingDeg,
          BearingCalculator.calculateDistanceMeters(myPos, _partnerLocation!),
        );
      } else {
        // Sin pareja conocida, solo notificar posición
        onLocationUpdate?.call(position, 0, 0);
      }

      notifyListeners();

      print('[GPS] Update #$_updateCount: '
          '${position.latitude.toStringAsFixed(5)}, '
          '${position.longitude.toStringAsFixed(5)} '
          '±${position.accuracy.toStringAsFixed(0)}m '
          '[${_currentMode.label}]');
    } catch (e) {
      print('[GPS] Error getting position: $e');
    }
  }

  /// Forzar una actualización GPS inmediata (útil para transiciones)
  Future<void> forceUpdate() async {
    if (_isRunning) {
      await _doGpsUpdate();
    }
  }

  // ── Estadísticas ───────────────────────────────────────────────────────

  /// Descripción legible del estado actual
  String get statusDescription {
    if (!_isRunning) return 'Detenido';
    if (_currentPosition == null) return 'Esperando GPS...';

    final distance = _distanceKm >= 0
        ? '${_distanceKm.toStringAsFixed(1)} km'
        : 'desconocida';
    return 'Activo [${_currentMode.label}] · Distancia: $distance · Updates: $_updateCount';
  }
}
