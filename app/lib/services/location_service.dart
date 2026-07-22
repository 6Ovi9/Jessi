import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:geolocator/geolocator.dart';
import 'package:latlong2/latlong.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'bearing_calculator.dart';

/// Servicio de localización GPS UI Proxy.
///
/// Recibe coordenadas GPS desde el Background Engine y notifica a la UI.
class LocationService extends ChangeNotifier {
  // ── Estado ──────────────────────────────────────────────────────────────

  /// Última posición GPS propia
  Position? _currentPosition;
  Position? get currentPosition => _currentPosition;

  /// Ubicación de la pareja (recibida de Supabase o cache local)
  LatLng? _partnerLocation;
  LatLng? get partnerLocation => _partnerLocation;

  /// Modo de polling actual (mantenido para la UI)
  GpsPollingMode _currentMode = GpsPollingMode.near;
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

  // ── Callbacks ──────────────────────────────────────────────────────────

  /// Callback cuando se obtiene una nueva posición GPS
  void Function(Position position, double bearing, int distanceM)? onLocationUpdate;

  // ── Lifecycle ──────────────────────────────────────────────────────────

  /// Verificar y solicitar permisos de ubicación.
  Future<bool> checkAndRequestPermissions() async {
    final serviceEnabled = await Geolocator.isLocationServiceEnabled();
    if (!serviceEnabled) {
      throw Exception('Los servicios de ubicación (GPS) están desactivados en los ajustes rápidos de tu teléfono.');
    }

    var permission = await Geolocator.checkPermission();

    if (permission == LocationPermission.denied) {
      permission = await Geolocator.requestPermission();
      if (permission == LocationPermission.denied) {
        throw Exception('El permiso de ubicación fue denegado por el usuario.');
      }
    }

    if (permission == LocationPermission.deniedForever) {
      throw Exception('El permiso de ubicación está denegado permanentemente. Actívalo en los ajustes de tu teléfono.');
    }

    if (shouldUpgradeToAlwaysPermission(isAndroid: Platform.isAndroid, permission: permission)) {
      await Geolocator.openAppSettings();
      final upgraded = await Geolocator.checkPermission();
      permission = upgraded;
    }

    if (permission == LocationPermission.whileInUse || permission == LocationPermission.always) {
      print('[GPS Proxy] Location permission granted: $permission');
      return true;
    }

    throw Exception('Permiso de ubicación denegado.');
  }

  @visibleForTesting
  static bool shouldUpgradeToAlwaysPermission({
    required bool isAndroid,
    required LocationPermission permission,
  }) {
    return isAndroid && permission == LocationPermission.whileInUse;
  }

  /// Iniciar el servicio de localización en la UI (Proxy).
  Future<void> start() async {
    if (_isRunning) return;

    final hasPermission = await checkAndRequestPermissions();
    if (!hasPermission) {
      print('[GPS Proxy] Cannot start: no permission');
      throw Exception('Los servicios de ubicación o los permisos de GPS están desactivados.');
    }

    _isRunning = true;
    _currentMode = GpsPollingMode.near; // Default seguro
    notifyListeners();

    print('[GPS Proxy] Service started (Listening to Background Engine)');
  }

  /// Detener el servicio de localización
  void stop() {
    _isRunning = false;
    notifyListeners();
    print('[GPS Proxy] Service stopped');
  }

  @override
  void dispose() {
    stop();
    super.dispose();
  }

  // ── Actualizaciones desde Background Engine ────────────────────────────

  /// Recibir actualización de posición desde el Background Engine
  void updateCurrentLocation(double lat, double lng) {
    _isRunning = true;
    
    _currentPosition = Position(
      latitude: lat,
      longitude: lng,
      timestamp: DateTime.now(),
      accuracy: 0.0,
      altitude: 0.0,
      altitudeAccuracy: 0.0,
      heading: 0.0,
      headingAccuracy: 0.0,
      speed: 0.0,
      speedAccuracy: 0.0,
      isMocked: false,
    );
    _updateCount++;

    if (_partnerLocation == null) {
      SharedPreferences.getInstance().then((prefs) {
        final latStr = prefs.getString('cached_partner_lat');
        final lngStr = prefs.getString('cached_partner_lng');
        if (latStr != null && lngStr != null) {
          final pLat = double.tryParse(latStr);
          final pLng = double.tryParse(lngStr);
          if (pLat != null && pLng != null) {
            _partnerLocation = LatLng(pLat, pLng);
            if (_currentPosition != null) {
              final myPos = LatLng(_currentPosition!.latitude, _currentPosition!.longitude);
              _distanceKm = BearingCalculator.calculateDistanceKm(myPos, _partnerLocation!);
              if (_distanceKm >= 0.015) {
                _bearingDeg = BearingCalculator.calculateBearing(myPos, _partnerLocation!);
              }
              onLocationUpdate?.call(
                _currentPosition!,
                _bearingDeg,
                BearingCalculator.calculateDistanceMeters(myPos, _partnerLocation!),
              );
            }
            notifyListeners();
          }
        }
      }).catchError((_) {});
    }

    if (_partnerLocation != null) {
      final myPos = LatLng(lat, lng);
      _distanceKm = BearingCalculator.calculateDistanceKm(myPos, _partnerLocation!);
      if (_distanceKm >= 0.015) {
        _bearingDeg = BearingCalculator.calculateBearing(myPos, _partnerLocation!);
      }

      // Recalcular modo de polling
      final newMode = BearingCalculator.getPollingMode(_distanceKm, _radarModeActive);
      if (newMode != _currentMode) {
        print('[GPS Proxy] Polling mode changed: ${_currentMode.label} → ${newMode.label}');
        _currentMode = newMode;
      }

      onLocationUpdate?.call(
        _currentPosition!,
        _bearingDeg,
        BearingCalculator.calculateDistanceMeters(myPos, _partnerLocation!),
      );
    } else {
      onLocationUpdate?.call(_currentPosition!, 0, 0);
    }

    notifyListeners();

    print('[GPS Proxy] Update #$_updateCount: Lat $lat, Lng $lng | Dist: ${_distanceKm.toStringAsFixed(3)}km | Brg: ${_bearingDeg.toStringAsFixed(1)}°');
  }

  // ── Actualización de ubicación de la pareja ────────────────────────────

  void updatePartnerLocation(LatLng partnerLocation) {
    _partnerLocation = partnerLocation;
    SharedPreferences.getInstance().then((prefs) {
      prefs.setString('cached_partner_lat', partnerLocation.latitude.toString());
      prefs.setString('cached_partner_lng', partnerLocation.longitude.toString());
    }).catchError((_) {});

    if (_currentPosition != null) {
      final myPos = LatLng(
        _currentPosition!.latitude,
        _currentPosition!.longitude,
      );

      _distanceKm = BearingCalculator.calculateDistanceKm(myPos, partnerLocation);
      if (_distanceKm >= 0.015) {
        _bearingDeg = BearingCalculator.calculateBearing(myPos, partnerLocation);
      }

      // Recalcular modo de polling
      final newMode = BearingCalculator.getPollingMode(_distanceKm, _radarModeActive);
      if (newMode != _currentMode) {
        print('[GPS Proxy] Polling mode changed: ${_currentMode.label} → ${newMode.label}');
        _currentMode = newMode;
      }

      notifyListeners();
    }
  }

  Future<void> setRadarModeActive(bool active) async {
    if (_radarModeActive == active) return;

    _radarModeActive = active;
    print('[GPS Proxy] Radar mode: ${active ? "ON" : "OFF"}');

    if (active) {
      _currentMode = GpsPollingMode.precision;
    } else {
      if (_distanceKm >= 0) {
        _currentMode = BearingCalculator.getPollingMode(_distanceKm, false);
      }
    }

    notifyListeners();
  }

  /// Configurar intervalos de polling personalizados (Dummy)
  void setPollingIntervals({
    int? precisionS,
    int? nearS,
    int? farS,
    int? remoteMinS,
    int? remoteMaxS,
  }) {
    // Los intervalos ahora son gestionados por BackgroundEngine.
    // Este método se mantiene por compatibilidad con main.dart.
  }

  // ── Estadísticas ───────────────────────────────────────────────────────

  String get statusDescription {
    if (!_isRunning) return 'Detenido';
    if (_currentPosition == null) return 'Esperando GPS...';

    final distance = _distanceKm >= 0
        ? '${_distanceKm.toStringAsFixed(1)} km'
        : 'desconocida';
    return 'Activo [${_currentMode.label}] · Distancia: $distance · Updates: $_updateCount';
  }
}
