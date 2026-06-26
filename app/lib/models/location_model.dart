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

  const LocationModel({
    required this.userId,
    required this.latitude,
    required this.longitude,
    this.accuracy = 0,
    this.pollingMode = 'NEAR',
    DateTime? updatedAt,
  }) : updatedAt = updatedAt ?? const _DefaultDateTime();

  /// Crear desde un mapa JSON (respuesta de Supabase)
  factory LocationModel.fromJson(Map<String, dynamic> json) {
    return LocationModel(
      userId: json['user_id'] as String,
      latitude: (json['latitude'] as num).toDouble(),
      longitude: (json['longitude'] as num).toDouble(),
      accuracy: (json['accuracy'] as num?)?.toDouble() ?? 0,
      pollingMode: json['polling_mode'] as String? ?? 'NEAR',
      updatedAt: json['updated_at'] != null
          ? DateTime.parse(json['updated_at'] as String)
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
    };
  }

  /// ¿Es una ubicación válida? (no es el valor inicial 0,0)
  bool get isValid => latitude != 0 || longitude != 0;

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

/// Clase auxiliar para tener un DateTime const por defecto.
/// Dart no permite DateTime.now() como valor default en constructores const.
class _DefaultDateTime implements DateTime {
  const _DefaultDateTime();

  DateTime get _now => DateTime.now();

  @override
  bool get isUtc => _now.isUtc;
  @override
  int get year => _now.year;
  @override
  int get month => _now.month;
  @override
  int get day => _now.day;
  @override
  int get hour => _now.hour;
  @override
  int get minute => _now.minute;
  @override
  int get second => _now.second;
  @override
  int get millisecond => _now.millisecond;
  @override
  int get microsecond => _now.microsecond;
  @override
  int get weekday => _now.weekday;
  @override
  String get timeZoneName => _now.timeZoneName;
  @override
  Duration get timeZoneOffset => _now.timeZoneOffset;
  @override
  int get millisecondsSinceEpoch => _now.millisecondsSinceEpoch;
  @override
  int get microsecondsSinceEpoch => _now.microsecondsSinceEpoch;

  @override
  bool isBefore(DateTime other) => _now.isBefore(other);
  @override
  bool isAfter(DateTime other) => _now.isAfter(other);
  @override
  bool isAtSameMomentAs(DateTime other) => _now.isAtSameMomentAs(other);
  @override
  int compareTo(DateTime other) => _now.compareTo(other);
  @override
  DateTime toLocal() => _now.toLocal();
  @override
  DateTime toUtc() => _now.toUtc();
  @override
  String toIso8601String() => _now.toIso8601String();
  @override
  DateTime add(Duration duration) => _now.add(duration);
  @override
  DateTime subtract(Duration duration) => _now.subtract(duration);
  @override
  Duration difference(DateTime other) => _now.difference(other);
  @override
  String toString() => _now.toString();
  @override
  bool operator ==(Object other) => _now == other;
  @override
  int get hashCode => _now.hashCode;
}
