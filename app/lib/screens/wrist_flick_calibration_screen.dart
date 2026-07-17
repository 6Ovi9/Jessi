import 'dart:async';

import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../services/ble_service.dart';
import '../models/config_model.dart';
import '../repositories/partner_repository.dart';

// ─────────────────────────────────────────────────────────────────────────────
// Constantes de hardware (deben coincidir con config.h del firmware)
// ─────────────────────────────────────────────────────────────────────────────
const int _kThresholdMin = 100;    // 100 dps
const int _kThresholdMax = 500;    // 500 dps
const int _kThresholdDefault = 260; // 260 dps

class WristFlickCalibrationScreen extends StatefulWidget {
  const WristFlickCalibrationScreen({super.key});

  @override
  State<WristFlickCalibrationScreen> createState() => _WristFlickCalibrationScreenState();
}

class _WristFlickCalibrationScreenState extends State<WristFlickCalibrationScreen>
    with SingleTickerProviderStateMixin {
  int _threshold = _kThresholdDefault;
  int _doubleFlickWindowMs = 800;
  bool _isDetected = false;
  Timer? _detectedTimer;
  int _totalDetections = 0;

  // ── Animations ───────────────────────────────────────────────────────────
  late AnimationController _detectCtrl;
  late Animation<double> _detectAnim;

  @override
  void initState() {
    super.initState();

    // Cargar umbral actual de la configuración
    final repo = context.read<PartnerRepository>();
    final cfg = repo.config ?? WatchConfig.defaultFor(repo.myUserId);
    _threshold = cfg.gyroThreshold.clamp(_kThresholdMin, _kThresholdMax);
    _doubleFlickWindowMs = cfg.doubleFlickWindowMs.clamp(400, 1200);

    // Animación de flash al detectar el gesto
    _detectCtrl = AnimationController(
      vsync: this,
      duration: const Duration(milliseconds: 350),
    );
    _detectAnim = CurvedAnimation(parent: _detectCtrl, curve: Curves.easeOut);

    // Suscribir callback de progreso de calibración desde el BLE
    final ble = context.read<BleService>();
    ble.onCalibrationProgress = _onCalibProgress;
  }

  @override
  void dispose() {
    _detectCtrl.dispose();
    _detectedTimer?.cancel();
    final ble = context.read<BleService>();
    ble.onCalibrationProgress = null;
    super.dispose();
  }

  // ── Handlers ─────────────────────────────────────────────────────────────

  void _onCalibProgress(int status) {
    if (!mounted) return;
    if (status == 0xFE) {
      // ¡Evento de detección de giro de muñeca notificado por el reloj!
      _simulateDetection();
    }
  }

  void _simulateDetection() {
    _detectedTimer?.cancel();
    setState(() {
      _isDetected = true;
      _totalDetections++;
    });
    _detectCtrl.forward(from: 0);
    _detectedTimer = Timer(const Duration(milliseconds: 900), () {
      if (mounted) setState(() => _isDetected = false);
    });
  }

  Future<void> _writeThreshold(int value) async {
    final ble = context.read<BleService>();
    if (ble.connectionState != BleConnectionState.connected) return;
    
    final repo = context.read<PartnerRepository>();
    final current = repo.config ?? WatchConfig.defaultFor(repo.myUserId);
    final cfg = current.copyWith(gyroThreshold: value);

    try {
      await repo.saveConfig(cfg);
    } catch (e) {
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('Error DB: $e'), backgroundColor: Colors.red),
      );
      return;
    }

    try {
      await ble.writeConfig(cfg);
    } catch (e) {
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('Error BLE: $e'), backgroundColor: Colors.orange),
      );
    }
  }

  Future<void> _writeDoubleFlickWindow(int value) async {
    final ble = context.read<BleService>();
    if (ble.connectionState != BleConnectionState.connected) return;
    
    final repo = context.read<PartnerRepository>();
    final current = repo.config ?? WatchConfig.defaultFor(repo.myUserId);
    final cfg = current.copyWith(doubleFlickWindowMs: value);

    try {
      await repo.saveConfig(cfg);
    } catch (e) {
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('Error DB: $e'), backgroundColor: Colors.red),
      );
      return;
    }

    try {
      await ble.writeConfig(cfg);
    } catch (e) {
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('Error BLE: $e'), backgroundColor: Colors.orange),
      );
    }
  }

  // ── Build ─────────────────────────────────────────────────────────────────

  @override
  Widget build(BuildContext context) {
    final ble = context.watch<BleService>();
    final connected = ble.connectionState == BleConnectionState.connected;

    return Scaffold(
      backgroundColor: const Color(0xFF080818),
      appBar: AppBar(
        backgroundColor: Colors.transparent,
        elevation: 0,
        leading: IconButton(
          icon: const Icon(Icons.arrow_back_ios_new_rounded, size: 20, color: Colors.white),
          onPressed: () => Navigator.pop(context),
        ),
        title: const Text(
          'Calibración del Giro',
          style: TextStyle(fontWeight: FontWeight.w700, letterSpacing: -0.5, color: Colors.white),
        ),
      ),
      body: Column(
        children: [
          // ── Advertencia de conexión ────────────────────────────────────────
          if (!connected)
            Padding(
              padding: const EdgeInsets.fromLTRB(20, 0, 20, 0),
              child: Container(
                padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 10),
                decoration: BoxDecoration(
                  color: const Color(0xFFFF6644).withValues(alpha: 0.12),
                  borderRadius: BorderRadius.circular(10),
                  border: Border.all(color: const Color(0xFFFF6644).withValues(alpha: 0.3)),
                ),
                child: const Row(
                  children: [
                    Icon(Icons.bluetooth_disabled_rounded,
                        color: Color(0xFFFF6644), size: 18),
                    SizedBox(width: 10),
                    Expanded(
                      child: Text(
                        'Conecta el reloj para calibración interactiva',
                        style: TextStyle(color: Color(0xFFFF6644), fontSize: 12),
                      ),
                    ),
                  ],
                ),
              ),
            ),

          Expanded(
            child: SingleChildScrollView(
              padding: const EdgeInsets.fromLTRB(20, 20, 20, 40),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  // Explicación
                  const _InfoBox(
                    icon: Icons.sync_problem_rounded,
                    text: 'El reloj detecta giros rápidos de muñeca para cambiar de menú. '
                        'Ajusta el umbral (en grados por segundo) y pruébalo en tiempo real: '
                        'valores más bajos hacen el gesto más fácil de activar, pero pueden dar falsos positivos; '
                        'valores más altos requieren un movimiento más firme y deliberado.',
                  ),
                  const SizedBox(height: 28),

                  // Indicador de Detección
                  _DetectionIndicator(
                    isDetected: _isDetected,
                    detectAnim: _detectAnim,
                    totalDetections: _totalDetections,
                    onTap: null,
                  ),
                  const SizedBox(height: 32),

                  // Cabecera del Slider del Umbral
                  Row(
                    mainAxisAlignment: MainAxisAlignment.spaceBetween,
                    children: [
                      const Text(
                        'Umbral de velocidad de giro',
                        style: TextStyle(
                          color: Colors.white,
                          fontWeight: FontWeight.w600,
                          fontSize: 14,
                        ),
                      ),
                      Container(
                        padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 4),
                        decoration: BoxDecoration(
                          color: const Color(0xFF00CC88).withValues(alpha: 0.15),
                          borderRadius: BorderRadius.circular(8),
                          border: Border.all(color: const Color(0xFF00CC88).withValues(alpha: 0.3)),
                        ),
                        child: Text(
                          '$_threshold dps',
                          style: const TextStyle(
                            color: Color(0xFF00FFCC),
                            fontWeight: FontWeight.w700,
                            fontSize: 13,
                            fontFamily: 'monospace',
                          ),
                        ),
                      ),
                    ],
                  ),
                  const SizedBox(height: 12),

                  SliderTheme(
                    data: SliderTheme.of(context).copyWith(
                      activeTrackColor: const Color(0xFF00CC88),
                      inactiveTrackColor: Colors.white.withValues(alpha: 0.08),
                      thumbColor: Colors.white,
                      overlayColor: const Color(0xFF00CC88).withValues(alpha: 0.2),
                      thumbShape: const RoundSliderThumbShape(enabledThumbRadius: 10),
                      trackHeight: 5,
                    ),
                    child: Slider(
                      value: _threshold.toDouble(),
                      min: _kThresholdMin.toDouble(),
                      max: _kThresholdMax.toDouble(),
                      divisions: (_kThresholdMax - _kThresholdMin) ~/ 10,
                      onChanged: connected
                          ? (v) {
                              setState(() => _threshold = v.round());
                            }
                          : null,
                      onChangeEnd: connected
                          ? (v) async {
                              await _writeThreshold(v.round());
                            }
                          : null,
                    ),
                  ),

                  Row(
                    mainAxisAlignment: MainAxisAlignment.spaceBetween,
                    children: [
                      Text(
                        'Más sensible\n$_kThresholdMin dps',
                        style: TextStyle(
                            color: Colors.white.withValues(alpha: 0.3),
                            fontSize: 10,
                            height: 1.4),
                      ),
                      Text(
                        'Menos sensible\n$_kThresholdMax dps',
                        textAlign: TextAlign.right,
                        style: TextStyle(
                            color: Colors.white.withValues(alpha: 0.3),
                            fontSize: 10,
                            height: 1.4),
                      ),
                    ],
                  ),
                  const SizedBox(height: 28),

                  // Cabecera del Slider del Timing del Doble Giro
                  Row(
                    mainAxisAlignment: MainAxisAlignment.spaceBetween,
                    children: [
                      const Text(
                        'Ventana de doble giro',
                        style: TextStyle(
                          color: Colors.white,
                          fontWeight: FontWeight.w600,
                          fontSize: 14,
                        ),
                      ),
                      Container(
                        padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 4),
                        decoration: BoxDecoration(
                          color: const Color(0xFF00CC88).withValues(alpha: 0.15),
                          borderRadius: BorderRadius.circular(8),
                          border: Border.all(color: const Color(0xFF00CC88).withValues(alpha: 0.3)),
                        ),
                        child: Text(
                          '$_doubleFlickWindowMs ms',
                          style: const TextStyle(
                            color: Color(0xFF00FFCC),
                            fontWeight: FontWeight.w700,
                            fontSize: 13,
                            fontFamily: 'monospace',
                          ),
                        ),
                      ),
                    ],
                  ),
                  const SizedBox(height: 12),

                  SliderTheme(
                    data: SliderTheme.of(context).copyWith(
                      activeTrackColor: const Color(0xFF00CC88),
                      inactiveTrackColor: Colors.white.withValues(alpha: 0.08),
                      thumbColor: Colors.white,
                      overlayColor: const Color(0xFF00CC88).withValues(alpha: 0.2),
                      thumbShape: const RoundSliderThumbShape(enabledThumbRadius: 10),
                      trackHeight: 5,
                    ),
                    child: Slider(
                      value: _doubleFlickWindowMs.toDouble(),
                      min: 400,
                      max: 1200,
                      divisions: 16,
                      onChanged: connected
                          ? (v) {
                              setState(() => _doubleFlickWindowMs = v.round());
                            }
                          : null,
                      onChangeEnd: connected
                          ? (v) async {
                              await _writeDoubleFlickWindow(v.round());
                            }
                          : null,
                    ),
                  ),

                  Row(
                    mainAxisAlignment: MainAxisAlignment.spaceBetween,
                    children: [
                      Text(
                        'Rápido (Exigente)\n400 ms',
                        style: TextStyle(
                            color: Colors.white.withValues(alpha: 0.3),
                            fontSize: 10,
                            height: 1.4),
                      ),
                      Text(
                        'Lento (Fácil)\n1200 ms',
                        textAlign: TextAlign.right,
                        style: TextStyle(
                            color: Colors.white.withValues(alpha: 0.3),
                            fontSize: 10,
                            height: 1.4),
                      ),
                    ],
                  ),
                  const SizedBox(height: 28),

                  // Presets rápidos
                  const _SectionLabel('PRESETS RÁPIDOS'),
                  const SizedBox(height: 12),
                  _PresetRow(
                    current: _threshold,
                    presets: const [
                      _Preset('Hiper-sensible', 150, 'Giro muy suave'),
                      _Preset('Sensible', 200, 'Movimiento leve'),
                      _Preset('Normal', 260, 'Giro estándar'),
                      _Preset('Firme', 340, 'Giro decidido'),
                      _Preset('Fuerte', 420, 'Giro brusco'),
                    ],
                    onSelect: (val) async {
                      setState(() => _threshold = val);
                      await _writeThreshold(val);
                    },
                  ),
                  const SizedBox(height: 28),

                  // Instrucciones
                  const _SectionLabel('CÓMO PROBAR EL GESTO'),
                  const SizedBox(height: 10),
                  const _StepItem(
                      icon: Icons.watch_rounded,
                      text: 'Ponte el reloj firmemente en la muñeca'),
                  const _StepItem(
                      icon: Icons.screen_rotation_alt_rounded,
                      text: 'Realiza un giro rápido de muñeca hacia afuera o hacia adentro'),
                  const _StepItem(
                      icon: Icons.check_circle_outline_rounded,
                      text: 'Si el indicador brilla en verde, el reloj ha detectado el gesto correctamente'),
                  const _StepItem(
                      icon: Icons.adjust_rounded,
                      text: 'Si se activa solo con el movimiento diario, sube el umbral. Si cuesta que detecte, bájalo.'),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Sub-widgets auxiliares
// ─────────────────────────────────────────────────────────────────────────────

class _DetectionIndicator extends StatelessWidget {
  final bool isDetected;
  final Animation<double> detectAnim;
  final int totalDetections;
  final VoidCallback? onTap;

  const _DetectionIndicator({
    required this.isDetected,
    required this.detectAnim,
    required this.totalDetections,
    this.onTap,
  });

  @override
  Widget build(BuildContext context) {
    return AnimatedBuilder(
      animation: detectAnim,
      builder: (context, _) {
        final flash = detectAnim.value;
        final baseColor =
            isDetected ? const Color(0xFF00CC88) : Colors.white.withValues(alpha: 0.06);
        final glowColor = isDetected
            ? Color.lerp(const Color(0xFF00CC88), const Color(0xFF00FF99), flash)!
            : Colors.transparent;
        final textColor =
            isDetected ? Colors.white : Colors.white.withValues(alpha: 0.25);

        return GestureDetector(
          onTap: onTap,
          child: Container(
            width: double.infinity,
            padding: const EdgeInsets.symmetric(vertical: 28),
            decoration: BoxDecoration(
              color: isDetected
                  ? glowColor.withValues(alpha: 0.10 + 0.05 * flash)
                  : baseColor,
              borderRadius: BorderRadius.circular(16),
              border: Border.all(
                color: isDetected
                    ? glowColor.withValues(alpha: 0.5 + 0.3 * flash)
                    : Colors.white.withValues(alpha: 0.06),
                width: 1.5,
              ),
              boxShadow: isDetected
                  ? [
                      BoxShadow(
                        color: const Color(0xFF00CC88)
                            .withValues(alpha: 0.15 + 0.1 * flash),
                        blurRadius: 28,
                        spreadRadius: 2,
                      )
                    ]
                  : null,
            ),
            child: Column(
              children: [
                AnimatedSwitcher(
                  duration: const Duration(milliseconds: 200),
                  child: Icon(
                    isDetected ? Icons.sync_rounded : Icons.sync_disabled_rounded,
                    key: ValueKey(isDetected),
                    size: 40,
                    color: isDetected ? glowColor : textColor,
                  ),
                ),
                const SizedBox(height: 10),
                AnimatedDefaultTextStyle(
                  duration: const Duration(milliseconds: 200),
                  style: TextStyle(
                    color: isDetected ? glowColor : textColor,
                    fontSize: 17,
                    fontWeight: FontWeight.w800,
                    letterSpacing: 2.0,
                  ),
                  child: Text(isDetected ? 'GIRO DETECTADO' : 'SIN DETECCIÓN'),
                ),
                const SizedBox(height: 5),
                Text(
                  isDetected
                      ? 'El reloj ha registrado el wrist flick ✓'
                      : 'Realiza un giro rápido de muñeca',
                  style: TextStyle(
                      color: textColor.withValues(alpha: 0.6), fontSize: 11),
                ),
                if (totalDetections > 0) ...[
                  const SizedBox(height: 10),
                  Text(
                    '$totalDetections detecciones en esta sesión',
                    style: TextStyle(
                        color: Colors.white.withValues(alpha: 0.18), fontSize: 10),
                  ),
                ],
              ],
            ),
          ),
        );
      },
    );
  }
}

class _InfoBox extends StatelessWidget {
  final IconData icon;
  final String text;
  const _InfoBox({required this.icon, required this.text});

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.all(14),
      decoration: BoxDecoration(
        color: Colors.white.withValues(alpha: 0.04),
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: Colors.white.withValues(alpha: 0.07)),
      ),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Icon(icon, color: const Color(0xFF00CC88), size: 18),
          const SizedBox(width: 10),
          Expanded(
            child: Text(
              text,
              style: TextStyle(
                color: Colors.white.withValues(alpha: 0.5),
                fontSize: 12,
                height: 1.5,
              ),
            ),
          ),
        ],
      ),
    );
  }
}

class _SectionLabel extends StatelessWidget {
  final String text;
  const _SectionLabel(this.text);

  @override
  Widget build(BuildContext context) {
    return Text(
      text,
      style: TextStyle(
        fontSize: 10,
        fontWeight: FontWeight.w700,
        color: Colors.white.withValues(alpha: 0.25),
        letterSpacing: 1.2,
      ),
    );
  }
}

class _StepItem extends StatelessWidget {
  final IconData icon;
  final String text;
  const _StepItem({required this.icon, required this.text});

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.only(bottom: 10),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Padding(
            padding: const EdgeInsets.only(top: 1),
            child: Icon(icon,
                size: 15, color: const Color(0xFF00CC88).withValues(alpha: 0.55)),
          ),
          const SizedBox(width: 10),
          Expanded(
            child: Text(
              text,
              style: TextStyle(
                color: Colors.white.withValues(alpha: 0.4),
                fontSize: 12,
                height: 1.4,
              ),
            ),
          ),
        ],
      ),
    );
  }
}

class _Preset {
  final String label;
  final int value;
  final String desc;
  const _Preset(this.label, this.value, this.desc);
}

class _PresetRow extends StatelessWidget {
  final int current;
  final List<_Preset> presets;
  final ValueChanged<int> onSelect;
  const _PresetRow(
      {required this.current, required this.presets, required this.onSelect});

  @override
  Widget build(BuildContext context) {
    return Wrap(
      spacing: 8,
      runSpacing: 8,
      children: presets.map((p) {
        final active = current == p.value;
        return GestureDetector(
          onTap: () => onSelect(p.value),
          child: AnimatedContainer(
            duration: const Duration(milliseconds: 180),
            padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
            decoration: BoxDecoration(
              color: active
                  ? const Color(0xFF00CC88).withValues(alpha: 0.18)
                  : Colors.white.withValues(alpha: 0.04),
              borderRadius: BorderRadius.circular(10),
              border: Border.all(
                color: active
                    ? const Color(0xFF00CC88).withValues(alpha: 0.5)
                    : Colors.white.withValues(alpha: 0.07),
                width: 1.5,
              ),
            ),
            child: Column(
              mainAxisSize: MainAxisSize.min,
              children: [
                Text(
                  p.label,
                  textAlign: TextAlign.center,
                  style: TextStyle(
                    color: active
                        ? const Color(0xFF00FFCC)
                        : Colors.white.withValues(alpha: 0.4),
                    fontSize: 11,
                    fontWeight: FontWeight.w600,
                    height: 1.3,
                  ),
                ),
                const SizedBox(height: 2),
                Text(
                  p.desc,
                  style: TextStyle(
                    color: active
                        ? const Color(0xFF00FFCC).withValues(alpha: 0.55)
                        : Colors.white.withValues(alpha: 0.2),
                    fontSize: 10,
                  ),
                ),
              ],
            ),
          ),
        );
      }).toList(),
    );
  }
}
