import 'dart:math';

import 'package:latlong2/latlong.dart';

/// Calculadora de rumbo (bearing) y distancia entre dos coordenadas GPS.
///
/// Usa la fórmula Haversine para distancia y atan2 para el bearing.
/// Este servicio es puro (sin estado ni side effects) y no necesita
/// inicialización.
class BearingCalculator {
  /// Radio de la Tierra en kilómetros
  static const double _earthRadiusKm = 6371.0;

  /// Instancia de Distance de latlong2 para cálculos
  static const Distance _distance = Distance();

  /// Calcular el bearing (rumbo) desde [from] hasta [to].
  ///
  /// Retorna un ángulo en grados [0, 360), donde:
  /// - 0° = Norte
  /// - 90° = Este
  /// - 180° = Sur
  /// - 270° = Oeste
  ///
  /// Este valor se envía al reloj por BLE (BEARING_CHAR).
  /// El reloj combina este bearing con su heading de brújula para
  /// calcular qué LED encender en RADAR_MODE.
  static double calculateBearing(LatLng from, LatLng to) {
    final lat1 = _toRadians(from.latitude);
    final lat2 = _toRadians(to.latitude);
    final dLon = _toRadians(to.longitude - from.longitude);

    final y = sin(dLon) * cos(lat2);
    final x = cos(lat1) * sin(lat2) - sin(lat1) * cos(lat2) * cos(dLon);

    // atan2 retorna [-π, π], lo convertimos a [0, 360)
    final bearing = (_toDegrees(atan2(y, x)) + 360) % 360;
    return bearing;
  }

  /// Calcular la distancia entre [from] y [to] en kilómetros.
  ///
  /// Usa el paquete latlong2 para el cálculo Haversine.
  /// El resultado se convierte a metros para enviar por BLE (DISTANCE_CHAR, uint32).
  static double calculateDistanceKm(LatLng from, LatLng to) {
    return _distance.as(LengthUnit.Kilometer, from, to);
  }

  /// Calcular la distancia en metros (para enviar por BLE como uint32).
  static int calculateDistanceMeters(LatLng from, LatLng to) {
    return (_distance.as(LengthUnit.Meter, from, to)).round();
  }

  /// Determinar el modo de polling GPS basado en la distancia.
  ///
  /// Retorna el intervalo recomendado en segundos según la tabla:
  /// - PRECISION: <500m o RADAR activo → 3s
  /// - NEAR: <10km → 60s
  /// - FAR: 10-50km → 180s
  /// - REMOTE: >50km → 300-600s (con jitter)
  static GpsPollingMode getPollingMode(double distanceKm, bool radarActive) {
    if (radarActive || distanceKm < 0.5) {
      return GpsPollingMode.precision;
    } else if (distanceKm < 10) {
      return GpsPollingMode.near;
    } else if (distanceKm < 50) {
      return GpsPollingMode.far;
    } else {
      return GpsPollingMode.remote;
    }
  }

  /// Obtener el intervalo de polling en segundos para un modo dado.
  ///
  /// [config] contiene los intervalos configurados por el usuario.
  /// Para REMOTE, aplica jitter aleatorio entre min y max.
  static Duration getPollingInterval(
    GpsPollingMode mode, {
    int precisionS = 3,
    int nearS = 60,
    int farS = 180,
    int remoteMinS = 300,
    int remoteMaxS = 600,
  }) {
    switch (mode) {
      case GpsPollingMode.precision:
        return Duration(seconds: precisionS);
      case GpsPollingMode.near:
        return Duration(seconds: nearS);
      case GpsPollingMode.far:
        return Duration(seconds: farS);
      case GpsPollingMode.remote:
        // Jitter aleatorio para evitar colisiones de polling entre ambos usuarios
        final jitter = Random().nextInt(remoteMaxS - remoteMinS + 1);
        return Duration(seconds: remoteMinS + jitter);
    }
  }

  /// Calcular el índice LED (0-11) desde un bearing relativo.
  ///
  /// [bearing] es el rumbo absoluto hacia la pareja (0-360°)
  /// [compassHeading] es el heading actual de la brújula del reloj (0-360°)
  ///
  /// El LED 0 = posición 12h (Norte del reloj).
  /// Fórmula: LED = round((bearing - heading + 360) % 360 / 360 * 12) % 12
  static int bearingToLedIndex(double bearing, double compassHeading) {
    final relative = (bearing - compassHeading + 360) % 360;
    return (relative / 360 * 12).round() % 12;
  }

  /// Calcular el número de LEDs a encender en DISTANCE_MODE.
  ///
  /// Retorna un valor entre 1 y 12 según la distancia.
  /// Los colores por rango se definen en config.h del firmware.
  static int distanceToLedCount(double distanceKm) {
    if (distanceKm >= 500) return 12;

    // Normalizar en escala de 0 a 1 (0 = 0km, 1 = 500km)
    final normalized = (distanceKm / 500).clamp(0.0, 1.0);

    // De 1 a 12 LEDs
    return (normalized * 11).floor() + 1;
  }

  // ── Utilidades privadas ────────────────────────────────────────────────

  static double _toRadians(double degrees) => degrees * pi / 180;
  static double _toDegrees(double radians) => radians * 180 / pi;
}

/// Modos de polling GPS dinámico.
///
/// El intervalo de actualización GPS escala según la distancia
/// para optimizar batería y tráfico de red.
enum GpsPollingMode {
  /// <500m o RADAR activo → 3s
  precision,

  /// <10km → 60s
  near,

  /// 10-50km → 3 min
  far,

  /// >50km → 5-10 min (con jitter)
  remote,
}

/// Extensión para nombres legibles de los modos
extension GpsPollingModeExtension on GpsPollingMode {
  String get label {
    switch (this) {
      case GpsPollingMode.precision:
        return 'PRECISION';
      case GpsPollingMode.near:
        return 'NEAR';
      case GpsPollingMode.far:
        return 'FAR';
      case GpsPollingMode.remote:
        return 'REMOTE';
    }
  }
}
