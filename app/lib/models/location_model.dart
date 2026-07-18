/// Modelo de ubicación GPS para sincronización con Supabase.
///
/// Representa la posición geográfica de un usuario (A o B)
/// tal como se almacena en la tabla `locations` de Supabase.
class LocationModel {
  final String userId;
  final double latitude;
  final double longitude;
  final double accuracy;
  final String pollingMode;
  final DateTime updatedAt;

  LocationModel({
    required this.userId,
    required this.latitude,
    required this.longitude,
    this.accuracy = 0,
    this.pollingMode = 'NEAR',
    DateTime? updatedAt,
  }) : updatedAt = updatedAt ?? DateTime.now();

  /// Crear desde un mapa JSON (respuesta de Supabase)
  factory LocationModel.fromJson(Map<String, dynamic> json) {
    return LocationModel(
      userId: json['user_id'] as String,
      latitude: (json['latitude'] as num?)?.toDouble() ?? 0.0,
      longitude: (json['longitude'] as num?)?.toDouble() ?? 0.0,
      accuracy: (json['accuracy'] as num?)?.toDouble() ?? 0,
      pollingMode: json['polling_mode'] as String? ?? 'NEAR',
      updatedAt: json['updated_at'] != null
          ? () {
              String dateStr = json['updated_at'] as String;
              if (!dateStr.endsWith('Z') && !dateStr.contains('+') && !dateStr.contains(RegExp(r'-\d{2}:\d{2}'))) {
                dateStr += 'Z';
              }
              return DateTime.parse(dateStr);
            }()
          : DateTime.now(),
    );
  }

  /// Convertir a mapa JSON para enviar a Supabase (UPSERT)
  Map<String, dynamic> toJson() {
    return {
      'user_id': userId,
      'latitude': latitude,
      'longitude': longitude,
      'accuracy': accuracy,
      'polling_mode': pollingMode,
      'updated_at': updatedAt.toUtc().toIso8601String(),
    };
  }

  /// ¿Es una ubicación válida? (no es el valor inicial 0,0)
  bool get isValid =>
      latitude >= -90 && latitude <= 90 &&
      longitude >= -180 && longitude <= 180;

  /// Edad de la ubicación (cuánto hace que se actualizó)
  Duration get age => DateTime.now().difference(updatedAt);

  @override
  String toString() =>
      'LocationModel($userId: $latitude, $longitude ±${accuracy.toStringAsFixed(0)}m '
      '[$pollingMode] ${age.inSeconds}s ago)';

  LocationModel copyWith({
    String? userId,
    double? latitude,
    double? longitude,
    double? accuracy,
    String? pollingMode,
    DateTime? updatedAt,
  }) {
    return LocationModel(
      userId: userId ?? this.userId,
      latitude: latitude ?? this.latitude,
      longitude: longitude ?? this.longitude,
      accuracy: accuracy ?? this.accuracy,
      pollingMode: pollingMode ?? this.pollingMode,
      updatedAt: updatedAt ?? this.updatedAt,
    );
  }
}

