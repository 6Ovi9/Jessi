import 'dart:ui';

/// Modelo de configuración del reloj.
///
/// Mapea 1:1 con la tabla `watch_config` de Supabase y los parámetros
/// configurables que se envían al reloj por BLE via CONFIG_CHAR (JSON).
class WatchConfig {
  final String userId;

  // Timers (segundos)
  final int clockTimeoutS;
  final int sleepTimeoutS;

  // Colores CLOCK_CONNECTED (hex AARRGGBB como String)
  final String colorHoursConnected;
  final String colorMinutesConnected;
  final String colorSecondsConnected;

  // Colores CLOCK_DISCONNECTED
  final String colorHoursDisc;
  final String colorMinutesDisc;
  final String colorSecondsDisc;

  // Brillo y batería
  final int brightnessPercent;
  final int lowBatteryThreshold;
  final bool logarithmicBrightness;

  // Vibración
  final String hapticPattern;

  // GPS Dynamic Polling (segundos)
  final int gpsIntervalPrecisionS;
  final int gpsIntervalNearS;
  final int gpsIntervalFarS;
  final int gpsIntervalRemoteMinS;
  final int gpsIntervalRemoteMaxS;

  // IMU wake-on-motion threshold
  final int wakeThreshold;

  // Gyroscope wrist-flick threshold (dps)
  final int gyroThreshold;

  // Double-flick window (ms)
  final int doubleFlickWindowMs;

  const WatchConfig({
    required this.userId,
    this.clockTimeoutS = 5,
    this.sleepTimeoutS = 5,
    this.colorHoursConnected = 'FFFFDCB4',
    this.colorMinutesConnected = 'FFFFF5F0',
    this.colorSecondsConnected = 'FFC8DCFF',
    this.colorHoursDisc = 'FF001478',
    this.colorMinutesDisc = 'FF003CC8',
    this.colorSecondsDisc = 'FF2864FF',
    this.brightnessPercent = 60,
    this.lowBatteryThreshold = 15,
    this.logarithmicBrightness = true,
    this.hapticPattern = 'both',
    this.gpsIntervalPrecisionS = 3,

    this.gpsIntervalNearS = 60,
    this.gpsIntervalFarS = 180,
    this.gpsIntervalRemoteMinS = 300,
    this.gpsIntervalRemoteMaxS = 600,
    this.wakeThreshold = 2,
    this.gyroThreshold = 260,
    this.doubleFlickWindowMs = 800,
  });

  /// Crear config por defecto para un usuario
  factory WatchConfig.defaultFor(String userId) {
    return WatchConfig(userId: userId);
  }

  static String _normalizeHapticPattern(String? rawValue) {
    final normalized = rawValue?.trim().toLowerCase();
    switch (normalized) {
      case 'partner':
      case '1':
        return 'partner';
      case 'both':
      case 'default':
      case 'all':
      case '0':
      case null:
      case '':
      default:
        return 'both';
    }
  }

  /// Crear desde JSON de Supabase
  factory WatchConfig.fromJson(Map<String, dynamic> json) {
    return WatchConfig(
      userId: json['user_id'] as String,
      clockTimeoutS: (json['clock_timeout_s'] as num?)?.toInt() ?? 5,
      sleepTimeoutS: (json['sleep_timeout_s'] as num?)?.toInt() ?? 5,
      colorHoursConnected: json['color_hours_connected'] as String? ?? 'FFFFDCB4',
      colorMinutesConnected: json['color_minutes_connected'] as String? ?? 'FFFFF5F0',
      colorSecondsConnected: json['color_seconds_connected'] as String? ?? 'FFC8DCFF',
      colorHoursDisc: json['color_hours_disc'] as String? ?? 'FF001478',
      colorMinutesDisc: json['color_minutes_disc'] as String? ?? 'FF003CC8',
      colorSecondsDisc: json['color_seconds_disc'] as String? ?? 'FF2864FF',
      brightnessPercent: (json['brightness_percent'] as num?)?.toInt() ?? 60,
      lowBatteryThreshold: (json['low_battery_threshold'] as num?)?.toInt() ?? 15,
      logarithmicBrightness: json['logarithmic_brightness'] as bool? ?? true,
      hapticPattern: _normalizeHapticPattern(json['haptic_pattern'] as String?),

      gpsIntervalPrecisionS: (json['gps_interval_precision_s'] as num?)?.toInt() ?? 3,
      gpsIntervalNearS: (json['gps_interval_near_s'] as num?)?.toInt() ?? 60,
      gpsIntervalFarS: (json['gps_interval_far_s'] as num?)?.toInt() ?? 180,
      gpsIntervalRemoteMinS: (json['gps_interval_remote_min_s'] as num?)?.toInt() ?? 300,
      gpsIntervalRemoteMaxS: (json['gps_interval_remote_max_s'] as num?)?.toInt() ?? 600,
      wakeThreshold: (json['wake_threshold'] as num?)?.toInt() ?? 2,
      gyroThreshold: (json['gyro_threshold'] as num?)?.toInt() ?? 260,
      doubleFlickWindowMs: (json['double_flick_window_ms'] as num?)?.toInt() ?? 800,
    );
  }

  /// Convertir a JSON para Supabase
  Map<String, dynamic> toJson() {
    return {
      'user_id': userId,
      'clock_timeout_s': clockTimeoutS,
      'sleep_timeout_s': sleepTimeoutS,
      'color_hours_connected': colorHoursConnected,
      'color_minutes_connected': colorMinutesConnected,
      'color_seconds_connected': colorSecondsConnected,
      'color_hours_disc': colorHoursDisc,
      'color_minutes_disc': colorMinutesDisc,
      'color_seconds_disc': colorSecondsDisc,
      'brightness_percent': brightnessPercent,
      'low_battery_threshold': lowBatteryThreshold,
      'logarithmic_brightness': logarithmicBrightness,
      'haptic_pattern': hapticPattern,
      'gps_interval_precision_s': gpsIntervalPrecisionS,
      'gps_interval_near_s': gpsIntervalNearS,
      'gps_interval_far_s': gpsIntervalFarS,
      'gps_interval_remote_min_s': gpsIntervalRemoteMinS,
      'gps_interval_remote_max_s': gpsIntervalRemoteMaxS,
      'wake_threshold': wakeThreshold,
      'gyro_threshold': gyroThreshold,
      'double_flick_window_ms': doubleFlickWindowMs,
    };
  }

  /// Convertir a JSON compacto para enviar por BLE (CONFIG_CHAR)
  /// Solo incluye los campos relevantes para el firmware
  Map<String, dynamic> toBleJson() {
    return {
      'ct': clockTimeoutS,
      'st': sleepTimeoutS,
      'chc': colorHoursConnected,
      'cmc': colorMinutesConnected,
      'csc': colorSecondsConnected,
      'chd': colorHoursDisc,
      'cmd': colorMinutesDisc,
      'csd': colorSecondsDisc,
      'br': brightnessPercent,
      'lb': lowBatteryThreshold,
      'lg': logarithmicBrightness ? 1 : 0,
      'hp': hapticPattern == 'partner' ? 1 : 0,
      'wt': wakeThreshold,
      'gt': gyroThreshold,
      'df': doubleFlickWindowMs,
    };
  }

  /// Parsear un color hex AARRGGBB a Color de Flutter
  static Color parseColor(String hexColor) {
    String hex = hexColor.trim().replaceAll('#', '');
    if (hex.length == 6) hex = 'FF$hex'; // Assume opaque if no alpha
    
    if (hex.length == 8) {
      final val = int.tryParse(hex, radix: 16);
      if (val != null) {
        return Color(val);
      }
    }
    return const Color(0xFFFFFFFF); // Fallback en caso de error
  }

  /// Color de Flutter para cada aguja del reloj
  Color get hoursConnectedColor => parseColor(colorHoursConnected);
  Color get minutesConnectedColor => parseColor(colorMinutesConnected);
  Color get secondsConnectedColor => parseColor(colorSecondsConnected);
  Color get hoursDiscColor => parseColor(colorHoursDisc);
  Color get minutesDiscColor => parseColor(colorMinutesDisc);
  Color get secondsDiscColor => parseColor(colorSecondsDisc);

  WatchConfig copyWith({
    String? userId,
    int? clockTimeoutS,
    int? sleepTimeoutS,
    String? colorHoursConnected,
    String? colorMinutesConnected,
    String? colorSecondsConnected,
    String? colorHoursDisc,
    String? colorMinutesDisc,
    String? colorSecondsDisc,
    int? brightnessPercent,
    int? lowBatteryThreshold,
    bool? logarithmicBrightness,
    String? hapticPattern,
    int? gpsIntervalPrecisionS,
    int? gpsIntervalNearS,
    int? gpsIntervalFarS,
    int? gpsIntervalRemoteMinS,
    int? gpsIntervalRemoteMaxS,
    int? wakeThreshold,
    int? gyroThreshold,
    int? doubleFlickWindowMs,
  }) {
    return WatchConfig(
      userId: userId ?? this.userId,
      clockTimeoutS: clockTimeoutS ?? this.clockTimeoutS,
      sleepTimeoutS: sleepTimeoutS ?? this.sleepTimeoutS,
      colorHoursConnected: colorHoursConnected ?? this.colorHoursConnected,
      colorMinutesConnected: colorMinutesConnected ?? this.colorMinutesConnected,
      colorSecondsConnected: colorSecondsConnected ?? this.colorSecondsConnected,
      colorHoursDisc: colorHoursDisc ?? this.colorHoursDisc,
      colorMinutesDisc: colorMinutesDisc ?? this.colorMinutesDisc,
      colorSecondsDisc: colorSecondsDisc ?? this.colorSecondsDisc,
      brightnessPercent: brightnessPercent ?? this.brightnessPercent,
      lowBatteryThreshold: lowBatteryThreshold ?? this.lowBatteryThreshold,
      logarithmicBrightness: logarithmicBrightness ?? this.logarithmicBrightness,
      hapticPattern: hapticPattern ?? this.hapticPattern,
      gpsIntervalPrecisionS: gpsIntervalPrecisionS ?? this.gpsIntervalPrecisionS,
      gpsIntervalNearS: gpsIntervalNearS ?? this.gpsIntervalNearS,
      gpsIntervalFarS: gpsIntervalFarS ?? this.gpsIntervalFarS,
      gpsIntervalRemoteMinS: gpsIntervalRemoteMinS ?? this.gpsIntervalRemoteMinS,
      gpsIntervalRemoteMaxS: gpsIntervalRemoteMaxS ?? this.gpsIntervalRemoteMaxS,
      wakeThreshold: wakeThreshold ?? this.wakeThreshold,
      gyroThreshold: gyroThreshold ?? this.gyroThreshold,
      doubleFlickWindowMs: doubleFlickWindowMs ?? this.doubleFlickWindowMs,
    );
  }
}
