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

  // Vibración y Hápticos Phase 2
  final String hapticPattern;
  final String colorHapticTx;
  final String colorHapticRx;
  final int brightnessHapticTx;
  final int brightnessHapticRx;

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

  // Triple-flick window (ms)
  final int tripleFlickWindowMs;

  // Compass / Radar color
  final String colorRadar;

  // Distance gauge colors
  final String colorDistanceNear;
  final String colorDistanceProv;
  final String colorDistanceFar;
  final String colorDistanceVFar;
  final String colorDistanceExtr;

  // Distance thresholds (km)
  final int distThresh1Km;
  final int distThresh2Km;
  final int distThresh3Km;
  final int distThresh4Km;
  final int distThreshMaxKm;

  // LED counts per distance zone (must sum to 12)
  final int ledsDistanceNear;
  final int ledsDistanceProv;
  final int ledsDistanceFar;
  final int ledsDistanceVFar;
  final int ledsDistanceExtr;

  final int updatedAt;

  const WatchConfig({
    required this.userId,
    this.clockTimeoutS = 5,
    this.sleepTimeoutS = 5,
    this.colorHoursConnected = 'FFFF3300',
    this.colorMinutesConnected = 'FF00FFCC',
    this.colorSecondsConnected = 'FFFF00FF',
    this.colorHoursDisc = 'FF001478',
    this.colorMinutesDisc = 'FF003CC8',
    this.colorSecondsDisc = 'FF2864FF',
    this.brightnessPercent = 15,
    this.lowBatteryThreshold = 15,
    this.logarithmicBrightness = true,
    this.hapticPattern = 'both',
    this.colorHapticTx = 'FF66CCFF',
    this.colorHapticRx = 'FFFF6699',
    this.brightnessHapticTx = 100,
    this.brightnessHapticRx = 100,
    this.gpsIntervalPrecisionS = 3,
    this.gpsIntervalNearS = 60,
    this.gpsIntervalFarS = 180,
    this.gpsIntervalRemoteMinS = 300,
    this.gpsIntervalRemoteMaxS = 600,
    this.wakeThreshold = 2,
    this.gyroThreshold = 260,
    this.doubleFlickWindowMs = 800,
    this.tripleFlickWindowMs = 1200,
    this.colorRadar = 'FF0080FF',
    this.colorDistanceNear = 'FF0080FF',
    this.colorDistanceProv = 'FF00CC44',
    this.colorDistanceFar = 'FFFFCC00',
    this.colorDistanceVFar = 'FFFF6600',
    this.colorDistanceExtr = 'FFFF0000',
    this.distThresh1Km = 15,
    this.distThresh2Km = 50,
    this.distThresh3Km = 150,
    this.distThresh4Km = 350,
    this.distThreshMaxKm = 500,
    this.ledsDistanceNear = 3,
    this.ledsDistanceProv = 3,
    this.ledsDistanceFar = 2,
    this.ledsDistanceVFar = 2,
    this.ledsDistanceExtr = 2,
    this.updatedAt = 0,
  });

  /// Crear una configuración por defecto para un usuario específico.
  factory WatchConfig.defaultFor(String userId) {
    return WatchConfig(userId: userId);
  }

  /// Crear desde JSON (Supabase DB o BLE)
  factory WatchConfig.fromJson(Map<String, dynamic> json) {
    return WatchConfig(
      userId: json['user_id'] as String? ?? '',
      clockTimeoutS: (json['clock_timeout_s'] as num?)?.toInt() ?? (json['ct'] as num?)?.toInt() ?? 5,
      sleepTimeoutS: (json['sleep_timeout_s'] as num?)?.toInt() ?? (json['st'] as num?)?.toInt() ?? 5,
      colorHoursConnected: json['color_hours_connected'] as String? ?? json['chc'] as String? ?? 'FFFF3300',
      colorMinutesConnected: json['color_minutes_connected'] as String? ?? json['cmc'] as String? ?? 'FF00FFCC',
      colorSecondsConnected: json['color_seconds_connected'] as String? ?? json['csc'] as String? ?? 'FFFF00FF',
      colorHoursDisc: json['color_hours_disc'] as String? ?? json['chd'] as String? ?? 'FF001478',
      colorMinutesDisc: json['color_minutes_disc'] as String? ?? json['cmd'] as String? ?? 'FF003CC8',
      colorSecondsDisc: json['color_seconds_disc'] as String? ?? json['csd'] as String? ?? 'FF2864FF',
      brightnessPercent: (json['brightness_percent'] as num?)?.toInt() ?? (json['br'] as num?)?.toInt() ?? 15,
      lowBatteryThreshold: (json['low_battery_threshold'] as num?)?.toInt() ?? (json['lb'] as num?)?.toInt() ?? 15,
      logarithmicBrightness: json['logarithmic_brightness'] as bool? ?? (json['lg'] == 1 || json['lg'] == true),
      hapticPattern: json['haptic_pattern'] as String? ?? (json['hp'] == 1 ? 'partner' : 'both'),
      colorHapticTx: json['color_haptic_tx'] as String? ?? json['ctx'] as String? ?? 'FF66CCFF',
      colorHapticRx: json['color_haptic_rx'] as String? ?? json['crx'] as String? ?? 'FFFF6699',
      brightnessHapticTx: (json['brightness_haptic_tx'] as num?)?.toInt() ?? (json['btx'] as num?)?.toInt() ?? 100,
      brightnessHapticRx: (json['brightness_haptic_rx'] as num?)?.toInt() ?? (json['brx'] as num?)?.toInt() ?? 100,
      gpsIntervalPrecisionS: (json['gps_interval_precision_s'] as num?)?.toInt() ?? 3,
      gpsIntervalNearS: (json['gps_interval_near_s'] as num?)?.toInt() ?? 60,
      gpsIntervalFarS: (json['gps_interval_far_s'] as num?)?.toInt() ?? 180,
      gpsIntervalRemoteMinS: (json['gps_interval_remote_min_s'] as num?)?.toInt() ?? 300,
      gpsIntervalRemoteMaxS: (json['gps_interval_remote_max_s'] as num?)?.toInt() ?? 600,
      wakeThreshold: (json['wake_threshold'] as num?)?.toInt() ?? (json['wt'] as num?)?.toInt() ?? 2,
      gyroThreshold: (json['gyro_threshold'] as num?)?.toInt() ?? (json['gt'] as num?)?.toInt() ?? 260,
      doubleFlickWindowMs: (json['double_flick_window_ms'] as num?)?.toInt() ?? (json['df'] as num?)?.toInt() ?? 800,
      tripleFlickWindowMs: (json['triple_flick_window_ms'] as num?)?.toInt() ?? (json['tf'] as num?)?.toInt() ?? 1200,
      colorRadar: json['color_radar'] as String? ?? json['cra'] as String? ?? 'FF0080FF',
      colorDistanceNear: json['color_distance_near'] as String? ?? json['cdn'] as String? ?? 'FF0080FF',
      colorDistanceProv: json['color_distance_prov'] as String? ?? json['cdp'] as String? ?? 'FF00CC44',
      colorDistanceFar: json['color_distance_far'] as String? ?? json['cdf'] as String? ?? 'FFFFCC00',
      colorDistanceVFar: json['color_distance_vfar'] as String? ?? json['cdv'] as String? ?? 'FFFF6600',
      colorDistanceExtr: json['color_distance_extr'] as String? ?? json['cde'] as String? ?? 'FFFF0000',
      distThresh1Km: (json['dist_thresh_1_km'] as num?)?.toInt() ?? (json['dt1'] as num?)?.toInt() ?? 15,
      distThresh2Km: (json['dist_thresh_2_km'] as num?)?.toInt() ?? (json['dt2'] as num?)?.toInt() ?? 50,
      distThresh3Km: (json['dist_thresh_3_km'] as num?)?.toInt() ?? (json['dt3'] as num?)?.toInt() ?? 150,
      distThresh4Km: (json['dist_thresh_4_km'] as num?)?.toInt() ?? (json['dt4'] as num?)?.toInt() ?? 350,
      distThreshMaxKm: (json['dist_thresh_max_km'] as num?)?.toInt() ?? (json['dtm'] as num?)?.toInt() ?? 500,
      ledsDistanceNear: (json['leds_distance_near'] as num?)?.toInt() ?? (json['ln'] as num?)?.toInt() ?? 3,
      ledsDistanceProv: (json['leds_distance_prov'] as num?)?.toInt() ?? (json['lp'] as num?)?.toInt() ?? 3,
      ledsDistanceFar: (json['leds_distance_far'] as num?)?.toInt() ?? (json['lf'] as num?)?.toInt() ?? 2,
      ledsDistanceVFar: (json['leds_distance_vfar'] as num?)?.toInt() ?? (json['lv'] as num?)?.toInt() ?? 2,
      ledsDistanceExtr: (json['leds_distance_extr'] as num?)?.toInt() ?? (json['le'] as num?)?.toInt() ?? 2,
      updatedAt: json['updated_at'] != null
          ? (json['updated_at'] is num
              ? (json['updated_at'] as num).toInt()
              : (DateTime.tryParse(json['updated_at'].toString())?.millisecondsSinceEpoch ?? 0))
          : ((json['updatedAt'] as num?)?.toInt() ?? 0),
    );
  }

  /// Convertir a JSON para Supabase
  Map<String, dynamic> toJson() {
    return {
      'user_id': userId,
      'updated_at': updatedAt == 0
          ? DateTime.now().toIso8601String()
          : DateTime.fromMillisecondsSinceEpoch(updatedAt).toIso8601String(),
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
      'color_haptic_tx': colorHapticTx,
      'color_haptic_rx': colorHapticRx,
      'brightness_haptic_tx': brightnessHapticTx,
      'brightness_haptic_rx': brightnessHapticRx,
      'gps_interval_precision_s': gpsIntervalPrecisionS,
      'gps_interval_near_s': gpsIntervalNearS,
      'gps_interval_far_s': gpsIntervalFarS,
      'gps_interval_remote_min_s': gpsIntervalRemoteMinS,
      'gps_interval_remote_max_s': gpsIntervalRemoteMaxS,
      'wake_threshold': wakeThreshold,
      'gyro_threshold': gyroThreshold,
      'double_flick_window_ms': doubleFlickWindowMs,
      'triple_flick_window_ms': tripleFlickWindowMs,
      'color_radar': colorRadar,
      'color_distance_near': colorDistanceNear,
      'color_distance_prov': colorDistanceProv,
      'color_distance_far': colorDistanceFar,
      'color_distance_vfar': colorDistanceVFar,
      'color_distance_extr': colorDistanceExtr,
      'dist_thresh_1_km': distThresh1Km,
      'dist_thresh_2_km': distThresh2Km,
      'dist_thresh_3_km': distThresh3Km,
      'dist_thresh_4_km': distThresh4Km,
      'dist_thresh_max_km': distThreshMaxKm,
      'leds_distance_near': ledsDistanceNear,
      'leds_distance_prov': ledsDistanceProv,
      'leds_distance_far': ledsDistanceFar,
      'leds_distance_vfar': ledsDistanceVFar,
      'leds_distance_extr': ledsDistanceExtr,
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
      'ctx': colorHapticTx,
      'crx': colorHapticRx,
      'btx': brightnessHapticTx,
      'brx': brightnessHapticRx,
      'wt': wakeThreshold,
      'gt': gyroThreshold,
      'df': doubleFlickWindowMs,
      'tf': tripleFlickWindowMs,
      'cra': colorRadar,
      'cdn': colorDistanceNear,
      'cdp': colorDistanceProv,
      'cdf': colorDistanceFar,
      'cdv': colorDistanceVFar,
      'cde': colorDistanceExtr,
      'dt1': distThresh1Km,
      'dt2': distThresh2Km,
      'dt3': distThresh3Km,
      'dt4': distThresh4Km,
      'dtm': distThreshMaxKm,
      'ln': ledsDistanceNear,
      'lp': ledsDistanceProv,
      'lf': ledsDistanceFar,
      'lv': ledsDistanceVFar,
      'le': ledsDistanceExtr,
    };
  }

  /// Convierte una cadena Hex (AARRGGBB, RRGGBB o #RRGGBB) a [Color]
  static Color parseColor(String hexColor) {
    var hex = hexColor.replaceAll('#', '').trim();
    if (hex.length == 6) {
      hex = 'FF$hex'; // Añadir alfa 100% si no viene especificado
    }
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
    String? colorHapticTx,
    String? colorHapticRx,
    int? brightnessHapticTx,
    int? brightnessHapticRx,
    int? gpsIntervalPrecisionS,
    int? gpsIntervalNearS,
    int? gpsIntervalFarS,
    int? gpsIntervalRemoteMinS,
    int? gpsIntervalRemoteMaxS,
    int? wakeThreshold,
    int? gyroThreshold,
    int? doubleFlickWindowMs,
    int? tripleFlickWindowMs,
    String? colorRadar,
    String? colorDistanceNear,
    String? colorDistanceProv,
    String? colorDistanceFar,
    String? colorDistanceVFar,
    String? colorDistanceExtr,
    int? distThresh1Km,
    int? distThresh2Km,
    int? distThresh3Km,
    int? distThresh4Km,
    int? distThreshMaxKm,
    int? ledsDistanceNear,
    int? ledsDistanceProv,
    int? ledsDistanceFar,
    int? ledsDistanceVFar,
    int? ledsDistanceExtr,
    int? updatedAt,
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
      colorHapticTx: colorHapticTx ?? this.colorHapticTx,
      colorHapticRx: colorHapticRx ?? this.colorHapticRx,
      brightnessHapticTx: brightnessHapticTx ?? this.brightnessHapticTx,
      brightnessHapticRx: brightnessHapticRx ?? this.brightnessHapticRx,
      gpsIntervalPrecisionS: gpsIntervalPrecisionS ?? this.gpsIntervalPrecisionS,
      gpsIntervalNearS: gpsIntervalNearS ?? this.gpsIntervalNearS,
      gpsIntervalFarS: gpsIntervalFarS ?? this.gpsIntervalFarS,
      gpsIntervalRemoteMinS: gpsIntervalRemoteMinS ?? this.gpsIntervalRemoteMinS,
      gpsIntervalRemoteMaxS: gpsIntervalRemoteMaxS ?? this.gpsIntervalRemoteMaxS,
      wakeThreshold: wakeThreshold ?? this.wakeThreshold,
      gyroThreshold: gyroThreshold ?? this.gyroThreshold,
      doubleFlickWindowMs: doubleFlickWindowMs ?? this.doubleFlickWindowMs,
      tripleFlickWindowMs: tripleFlickWindowMs ?? this.tripleFlickWindowMs,
      colorRadar: colorRadar ?? this.colorRadar,
      colorDistanceNear: colorDistanceNear ?? this.colorDistanceNear,
      colorDistanceProv: colorDistanceProv ?? this.colorDistanceProv,
      colorDistanceFar: colorDistanceFar ?? this.colorDistanceFar,
      colorDistanceVFar: colorDistanceVFar ?? this.colorDistanceVFar,
      colorDistanceExtr: colorDistanceExtr ?? this.colorDistanceExtr,
      distThresh1Km: distThresh1Km ?? this.distThresh1Km,
      distThresh2Km: distThresh2Km ?? this.distThresh2Km,
      distThresh3Km: distThresh3Km ?? this.distThresh3Km,
      distThresh4Km: distThresh4Km ?? this.distThresh4Km,
      distThreshMaxKm: distThreshMaxKm ?? this.distThreshMaxKm,
      ledsDistanceNear: ledsDistanceNear ?? this.ledsDistanceNear,
      ledsDistanceProv: ledsDistanceProv ?? this.ledsDistanceProv,
      ledsDistanceFar: ledsDistanceFar ?? this.ledsDistanceFar,
      ledsDistanceVFar: ledsDistanceVFar ?? this.ledsDistanceVFar,
      ledsDistanceExtr: ledsDistanceExtr ?? this.ledsDistanceExtr,
      updatedAt: updatedAt ?? this.updatedAt,
    );
  }
}
