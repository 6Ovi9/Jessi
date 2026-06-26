import 'dart:async';
import 'dart:math';

import 'package:flutter/material.dart';

import '../models/config_model.dart';

/// Widget de previsualización del reloj.
///
/// Dibuja un anillo de 12 LEDs que simula cómo se ve el reloj real
/// en cada modo (CLOCK, RADAR, DISTANCE). Permite al usuario ver
/// el efecto de los cambios de configuración en tiempo real.
class WatchPreviewWidget extends StatefulWidget {
  /// Modo de visualización
  final WatchPreviewMode mode;

  /// Configuración del reloj (colores, brillo)
  final WatchConfig config;

  /// ¿El reloj está conectado por BLE?
  final bool isConnected;

  /// Bearing relativo (para RADAR_MODE, 0-360)
  final double bearing;

  /// Distancia en km (para DISTANCE_MODE)
  final double distanceKm;

  /// Tamaño del widget
  final double size;

  const WatchPreviewWidget({
    super.key,
    this.mode = WatchPreviewMode.clock,
    required this.config,
    this.isConnected = true,
    this.bearing = 0,
    this.distanceKm = 0,
    this.size = 240,
  });

  @override
  State<WatchPreviewWidget> createState() => _WatchPreviewWidgetState();
}

class _WatchPreviewWidgetState extends State<WatchPreviewWidget> {
  /// Timer de 1 segundo para sincronizar el reloj con la hora real
  Timer? _clockTimer;
  DateTime _now = DateTime.now();

  @override
  void initState() {
    super.initState();
    // Tick cada 100ms para animación suave de segundos
    _clockTimer = Timer.periodic(const Duration(milliseconds: 100), (_) {
      if (mounted) {
        setState(() {
          _now = DateTime.now();
        });
      }
    });
  }

  @override
  void dispose() {
    _clockTimer?.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return CustomPaint(
      size: Size(widget.size, widget.size),
      painter: _WatchPainter(
        mode: widget.mode,
        config: widget.config,
        isConnected: widget.isConnected,
        bearing: widget.bearing,
        distanceKm: widget.distanceKm,
        now: _now,
      ),
    );
  }
}

/// Painter del reloj
class _WatchPainter extends CustomPainter {
  final WatchPreviewMode mode;
  final WatchConfig config;
  final bool isConnected;
  final double bearing;
  final double distanceKm;
  final DateTime now;

  _WatchPainter({
    required this.mode,
    required this.config,
    required this.isConnected,
    required this.bearing,
    required this.distanceKm,
    required this.now,
  });

  @override
  void paint(Canvas canvas, Size size) {
    final center = Offset(size.width / 2, size.height / 2);
    final radius = size.width / 2;
    final ledRadius = radius * 0.12;
    final ledRingRadius = radius * 0.78;

    // ── Fondo del reloj ────────────────────────────────────────────────
    _drawBackground(canvas, center, radius);

    // ── Anillo de LEDs ─────────────────────────────────────────────────
    switch (mode) {
      case WatchPreviewMode.clock:
        _drawClockMode(canvas, center, ledRingRadius, ledRadius);
        break;
      case WatchPreviewMode.radar:
        _drawRadarMode(canvas, center, ledRingRadius, ledRadius);
        break;
      case WatchPreviewMode.distance:
        _drawDistanceMode(canvas, center, ledRingRadius, ledRadius);
        break;
    }

    // ── Marcas de las horas ────────────────────────────────────────────
    _drawHourMarks(canvas, center, radius);
  }

  void _drawBackground(Canvas canvas, Offset center, double radius) {
    // Fondo oscuro del reloj
    final bgPaint = Paint()
      ..shader = const RadialGradient(
        colors: [
          Color(0xFF1A1A2E),
          Color(0xFF0F0F1A),
          Color(0xFF050510),
        ],
        stops: [0.0, 0.7, 1.0],
      ).createShader(Rect.fromCircle(center: center, radius: radius));
    canvas.drawCircle(center, radius, bgPaint);

    // Borde sutil
    final borderPaint = Paint()
      ..style = PaintingStyle.stroke
      ..strokeWidth = 2
      ..shader = SweepGradient(
        colors: [
          Colors.white.withValues(alpha: 0.1),
          Colors.white.withValues(alpha: 0.03),
          Colors.white.withValues(alpha: 0.1),
        ],
      ).createShader(Rect.fromCircle(center: center, radius: radius));
    canvas.drawCircle(center, radius - 1, borderPaint);
  }

  void _drawClockMode(
      Canvas canvas, Offset center, double ringRadius, double ledRadius) {
    final brightness = config.brightnessPercent / 100.0;

    // Calcular posiciones de las agujas con hora REAL
    // Horas: LED 0-11 (0 = 12h, 1 = 1h, ...)
    final hourPos = (now.hour % 12).toDouble() +
        now.minute / 60.0; // avance suave dentro de la hora
    // Minutos: 0-11 LEDs mapeados a 0-60 min
    final minutePos = now.minute / 60.0 * 12.0 + now.second / 3600.0 * 12.0;
    // Segundos: interpolación suave con milisegundos
    final secondPos =
        (now.second + now.millisecond / 1000.0) / 60.0 * 12.0;

    // Colores según estado de conexión
    final hourColor =
        isConnected ? config.hoursConnectedColor : config.hoursDiscColor;
    final minuteColor =
        isConnected ? config.minutesConnectedColor : config.minutesDiscColor;
    final secondColor =
        isConnected ? config.secondsConnectedColor : config.secondsDiscColor;

    for (int i = 0; i < 12; i++) {
      final angle = (i / 12) * 2 * pi - pi / 2; // 12h = arriba
      final pos = Offset(
        center.dx + cos(angle) * ringRadius,
        center.dy + sin(angle) * ringRadius,
      );

      // Determinar color y brillo de este LED
      Color ledColor = Colors.transparent;
      double ledBrightness = 0;

      // ¿Es el LED de la hora?
      if (i == hourPos.floor() % 12) {
        ledColor = hourColor;
        ledBrightness = brightness;
      }

      // ¿Es el LED de los minutos? (prioridad sobre horas)
      if (i == minutePos.floor() % 12) {
        ledColor = minuteColor;
        ledBrightness = brightness;
      }

      // ¿Es el LED de los segundos? (máxima prioridad — interpolación suave)
      final secondMain = secondPos.floor() % 12;
      final secondNext = (secondMain + 1) % 12;
      final secondFraction = secondPos - secondPos.floor();

      if (i == secondMain) {
        final gamma = config.logarithmicBrightness ? 2.2 : 1.0;
        final frac = brightness * pow(1.0 - secondFraction, gamma);
        if (frac > 0.02) {
          ledColor = secondColor;
          ledBrightness = frac.toDouble();
        }
      } else if (i == secondNext) {
        final gamma = config.logarithmicBrightness ? 2.2 : 1.0;
        final frac = brightness * pow(secondFraction, gamma);
        if (frac > 0.02) {
          ledColor = secondColor;
          ledBrightness = frac.toDouble();
        }
      }

      _drawLed(canvas, pos, ledRadius, ledColor, ledBrightness);
    }
  }

  void _drawRadarMode(
      Canvas canvas, Offset center, double ringRadius, double ledRadius) {
    final brightness = config.brightnessPercent / 100.0;
    const radarColor = Color(0xFFFFB900); // Ámbar cálido

    // Calcular LED index desde bearing
    final ledIndex = (bearing / 360 * 12).round() % 12;
    final ledFraction = (bearing / 360 * 12) - (bearing / 360 * 12).floor();

    for (int i = 0; i < 12; i++) {
      final angle = (i / 12) * 2 * pi - pi / 2;
      final pos = Offset(
        center.dx + cos(angle) * ringRadius,
        center.dy + sin(angle) * ringRadius,
      );

      double ledBrightness = 0;

      if (i == ledIndex) {
        ledBrightness = brightness * (1.0 - ledFraction * 0.5);
      } else if (i == (ledIndex + 1) % 12) {
        ledBrightness = brightness * ledFraction;
      }

      _drawLed(canvas, pos, ledRadius, radarColor, ledBrightness);
    }
  }

  void _drawDistanceMode(
      Canvas canvas, Offset center, double ringRadius, double ledRadius) {
    final brightness = config.brightnessPercent / 100.0;
    const maxDistance = 500.0; // km

    // Calculamos la distancia en escala logarítmica para ser sensibles a cortas distancias.
    // Idéntico al firmware en C++
    double dM = distanceKm * 1000.0;
    if (dM < 10.0) dM = 10.0; // Mínimo 10 metros
    
    final logDist = log(dM) / ln10; // log10(x) = ln(x) / ln(10)
    final normalized = ((logDist - 1.0) / 5.7).clamp(0.0, 1.0);
    
    final ledsFloat = 1.0 + normalized * 11.0;
    int ledsOn = ledsFloat.floor();
    double partialBrightness = ledsFloat - ledsOn;
    
    if (ledsOn >= 12) {
      ledsOn = 12;
      partialBrightness = 0.0;
    }

    // Colores por rango de distancia
    const distanceColors = [
      Color(0xFF0080FF), // Azul (0-15km) — LEDs 1-4
      Color(0xFF0080FF),
      Color(0xFF0080FF),
      Color(0xFF0080FF),
      Color(0xFF00CC44), // Verde (15-50km) — LEDs 5-7
      Color(0xFF00CC44),
      Color(0xFF00CC44),
      Color(0xFFFFCC00), // Amarillo (50-150km) — LEDs 8-9
      Color(0xFFFFCC00),
      Color(0xFFFF6600), // Naranja (150-350km) — LEDs 10-11
      Color(0xFFFF6600),
      Color(0xFFFF0000), // Rojo (350-500km) — LED 12
    ];

    for (int i = 0; i < 12; i++) {
      final angle = (i / 12) * 2 * pi - pi / 2;
      final pos = Offset(
        center.dx + cos(angle) * ringRadius,
        center.dy + sin(angle) * ringRadius,
      );

      double ledBrightness = 0;
      final color = distanceColors[i];

      if (i < ledsOn - 1) {
        ledBrightness = brightness;
      } else if (i == ledsOn - 1) {
        ledBrightness = brightness * partialBrightness;
      }

      _drawLed(canvas, pos, ledRadius, color, ledBrightness);
    }
  }

  void _drawLed(
      Canvas canvas, Offset pos, double radius, Color color, double brightness) {
    // LED base (siempre visible, muy tenue)
    final basePaint = Paint()
      ..color = Colors.white.withValues(alpha: 0.03)
      ..style = PaintingStyle.fill;
    canvas.drawCircle(pos, radius, basePaint);

    if (brightness <= 0.01) return;

    // LED encendido
    final ledPaint = Paint()
      ..color = color.withValues(alpha: brightness.clamp(0.0, 1.0))
      ..style = PaintingStyle.fill;
    canvas.drawCircle(pos, radius, ledPaint);

    // Glow effect
    final glowPaint = Paint()
      ..color = color.withValues(alpha: (brightness * 0.4).clamp(0.0, 0.4))
      ..maskFilter = MaskFilter.blur(BlurStyle.normal, radius * 1.5);
    canvas.drawCircle(pos, radius * 1.2, glowPaint);
  }

  void _drawHourMarks(Canvas canvas, Offset center, double radius) {
    final markPaint = Paint()
      ..color = Colors.white.withValues(alpha: 0.15)
      ..strokeWidth = 1.5
      ..strokeCap = StrokeCap.round;

    for (int i = 0; i < 12; i++) {
      final angle = (i / 12) * 2 * pi - pi / 2;
      final innerR = radius * 0.92;
      final outerR = radius * 0.97;
      final start = Offset(
        center.dx + cos(angle) * innerR,
        center.dy + sin(angle) * innerR,
      );
      final end = Offset(
        center.dx + cos(angle) * outerR,
        center.dy + sin(angle) * outerR,
      );

      // Marcas más gruesas en 12, 3, 6, 9
      if (i % 3 == 0) {
        markPaint.strokeWidth = 2.5;
        markPaint.color = Colors.white.withValues(alpha: 0.25);
      } else {
        markPaint.strokeWidth = 1.5;
        markPaint.color = Colors.white.withValues(alpha: 0.12);
      }

      canvas.drawLine(start, end, markPaint);
    }
  }

  @override
  bool shouldRepaint(covariant _WatchPainter oldDelegate) {
    return oldDelegate.now != now ||
        oldDelegate.mode != mode ||
        oldDelegate.bearing != bearing ||
        oldDelegate.distanceKm != distanceKm ||
        oldDelegate.isConnected != isConnected ||
        oldDelegate.config != config;
  }
}

/// Modos de previsualización del reloj
enum WatchPreviewMode {
  clock,
  radar,
  distance,
}
