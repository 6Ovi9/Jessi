import 'dart:async';

import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../services/ble_service.dart';
import '../models/config_model.dart';
import '../repositories/partner_repository.dart';

// ─────────────────────────────────────────────────────────────────────────────
// Constantes de hardware (deben coincidir con config.h del firmware)
// 1 LSB = 62.5 mg a ±2G, campo de 6 bits
// ─────────────────────────────────────────────────────────────────────────────
const int _kThresholdMin = 1; // 62.5 mg
const int _kThresholdMax = 32; // 2000 mg (práctico)
const int _kThresholdDefault = 8; // 500 mg

double _thresholdToMg(int reg) => reg * 62.5;
int _mgToThreshold(double mg) =>
    (mg / 62.5).round().clamp(_kThresholdMin, _kThresholdMax);

enum _CalibMode { manual, auto }

// ─────────────────────────────────────────────────────────────────────────────

class WakeCalibrationScreen extends StatefulWidget {
  const WakeCalibrationScreen({super.key});

  @override
  State<WakeCalibrationScreen> createState() => _WakeCalibrationScreenState();
}

class _WakeCalibrationScreenState extends State<WakeCalibrationScreen>
    with TickerProviderStateMixin {
  _CalibMode _mode = _CalibMode.manual;

  // ── Manual mode ──────────────────────────────────────────────────────────
  int _threshold = _kThresholdDefault;
  bool _isDetected = false;
  Timer? _detectedTimer;
  int _totalDetections = 0;

  // ── Auto mode ────────────────────────────────────────────────────────────
  bool _calibRunning = false;
  int _calibProgress = 0;
  bool _calibDone = false;
  int? _calibResultThreshold;
  List<bool> _gestureSlots = List.filled(5, false);

  // ── Animations ───────────────────────────────────────────────────────────
  late AnimationController _pulseCtrl;
  late Animation<double> _pulseAnim;
  late AnimationController _detectCtrl;
  late Animation<double> _detectAnim;

  @override
  void initState() {
    super.initState();

    // Load current threshold from config
    final repo = context.read<PartnerRepository>();
    final cfg = repo.config ?? WatchConfig.defaultFor(repo.myUserId);
    _threshold = cfg.wakeThreshold.clamp(_kThresholdMin, _kThresholdMax);

    // Pulse animation for "waiting for gesture" in auto mode
    _pulseCtrl = AnimationController(
      vsync: this,
      duration: const Duration(milliseconds: 1400),
    )..repeat(reverse: true);
    _pulseAnim = CurvedAnimation(parent: _pulseCtrl, curve: Curves.easeInOut);

    // Flash animation when gesture detected
    _detectCtrl = AnimationController(
      vsync: this,
      duration: const Duration(milliseconds: 350),
    );
    _detectAnim = CurvedAnimation(parent: _detectCtrl, curve: Curves.easeOut);

    // Hook calibration progress from BLE
    final ble = context.read<BleService>();
    ble.onCalibrationProgress = _onCalibProgress;
  }

  @override
  void dispose() {
    _pulseCtrl.dispose();
    _detectCtrl.dispose();
    _detectedTimer?.cancel();
    final ble = context.read<BleService>();
    ble.onCalibrationProgress = null;
    super.dispose();
  }

  // ── Handlers ─────────────────────────────────────────────────────────────

  void _onCalibProgress(int samplesDone) {
    if (!mounted) return;
    if (samplesDone == 0xFF) {
      // Manual motion detection event notified by the watch!
      _simulateDetection();
      return;
    }
    setState(() {
      _calibProgress = samplesDone.clamp(0, 5);
      for (int i = 0; i < _calibProgress; i++) {
        _gestureSlots[i] = true;
      }
      if (_calibProgress >= 5) {
        _calibDone = true;
        _calibRunning = false;
        // Read the threshold calculated by the watch from the BLE service
        final ble = context.read<BleService>();
        _calibResultThreshold = ble.calibThreshold ?? _kThresholdDefault;
      }
    });
    _detectCtrl.forward(from: 0);
  }

  /// Simulate a detection flash — called by the debug loop if INT1 fires.
  /// In production, the watch can notify via the calib_status characteristic
  /// whenever the accel exceeds threshold during manual test mode.
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

  Future<void> _writeThreshold(int reg) async {
    final ble = context.read<BleService>();
    if (ble.connectionState != BleConnectionState.connected) return;
    await ble.writeWakeThreshold(reg);
    final repo = context.read<PartnerRepository>();
    final cfg = repo.config ?? WatchConfig.defaultFor(repo.myUserId);
    await repo.saveConfig(cfg.copyWith(wakeThreshold: reg));
  }

  Future<void> _startAutoCalib() async {
    final ble = context.read<BleService>();
    if (ble.connectionState != BleConnectionState.connected) return;
    setState(() {
      _calibRunning = true;
      _calibDone = false;
      _calibProgress = 0;
      _calibResultThreshold = null;
      _gestureSlots = List.filled(5, false);
    });
    await ble.startCalibration();
  }

  Future<void> _cancelAutoCalib() async {
    final ble = context.read<BleService>();
    await ble.cancelCalibration();
    setState(() {
      _calibRunning = false;
      _calibProgress = 0;
      _gestureSlots = List.filled(5, false);
    });
  }

  Future<void> _applyAutoResult() async {
    if (_calibResultThreshold == null) return;
    await _writeThreshold(_calibResultThreshold!);
    setState(() => _threshold = _calibResultThreshold!);
    if (mounted) {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: const Text('Umbral guardado en el reloj ✓'),
          backgroundColor: const Color(0xFF00AA66),
          behavior: SnackBarBehavior.floating,
          shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(8)),
        ),
      );
      Navigator.pop(context);
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
          icon: const Icon(Icons.arrow_back_ios_new_rounded, size: 20),
          onPressed: () => Navigator.pop(context),
        ),
        title: const Text(
          'Rise-to-Wake',
          style: TextStyle(fontWeight: FontWeight.w700, letterSpacing: -0.5),
        ),
      ),
      body: Column(
        children: [
          // ── Connection warning ──────────────────────────────────────────
          if (!connected)
            Padding(
              padding: const EdgeInsets.fromLTRB(20, 0, 20, 0),
              child: Container(
                padding:
                    const EdgeInsets.symmetric(horizontal: 16, vertical: 10),
                decoration: BoxDecoration(
                  color: const Color(0xFFFF6644).withValues(alpha: 0.12),
                  borderRadius: BorderRadius.circular(10),
                  border: Border.all(
                      color: const Color(0xFFFF6644).withValues(alpha: 0.3)),
                ),
                child: const Row(
                  children: [
                    Icon(Icons.bluetooth_disabled_rounded,
                        color: Color(0xFFFF6644), size: 18),
                    SizedBox(width: 10),
                    Expanded(
                      child: Text(
                        'Conecta el reloj para calibración interactiva',
                        style:
                            TextStyle(color: Color(0xFFFF6644), fontSize: 12),
                      ),
                    ),
                  ],
                ),
              ),
            ),

          // ── Mode tabs ───────────────────────────────────────────────────
          Padding(
            padding: const EdgeInsets.fromLTRB(20, 20, 20, 0),
            child: _ModeTabBar(
              selected: _mode,
              onChanged: (m) => setState(() => _mode = m),
            ),
          ),

          Expanded(
            child: AnimatedSwitcher(
              duration: const Duration(milliseconds: 250),
              child: _mode == _CalibMode.manual
                  ? _buildManualMode(connected)
                  : _buildAutoMode(connected),
            ),
          ),
        ],
      ),
    );
  }

  // ─────────────────────────────────────────────────────────────────────────
  // MANUAL MODE
  // ─────────────────────────────────────────────────────────────────────────

  Widget _buildManualMode(bool connected) {
    final threshMg = _thresholdToMg(_threshold);

    return SingleChildScrollView(
      key: const ValueKey('manual'),
      padding: const EdgeInsets.fromLTRB(20, 28, 20, 40),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          // Explainer
          const _InfoBox(
            icon: Icons.tune_rounded,
            text: 'Ajusta el umbral y mueve el reloj. El indicador se activa '
                'si el chip lo detectaría como gesto de wake. '
                'Sube si despierta solo; baja si no detecta el giro de muñeca.',
          ),
          const SizedBox(height: 28),

          // Detection indicator
          _DetectionIndicator(
            isDetected: _isDetected,
            detectAnim: _detectAnim,
            totalDetections: _totalDetections,
            onTap: connected ? _simulateDetection : null,
          ),
          const SizedBox(height: 32),

          // Threshold slider header
          Row(
            mainAxisAlignment: MainAxisAlignment.spaceBetween,
            children: [
              const Text(
                'Umbral de sensibilidad',
                style: TextStyle(
                  color: Colors.white,
                  fontWeight: FontWeight.w600,
                  fontSize: 14,
                ),
              ),
              Container(
                padding:
                    const EdgeInsets.symmetric(horizontal: 10, vertical: 4),
                decoration: BoxDecoration(
                  color: const Color(0xFF8866FF).withValues(alpha: 0.15),
                  borderRadius: BorderRadius.circular(8),
                  border: Border.all(
                      color: const Color(0xFF8866FF).withValues(alpha: 0.3)),
                ),
                child: Text(
                  '${threshMg.toInt()} mg',
                  style: const TextStyle(
                    color: Color(0xFFBB99FF),
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
              activeTrackColor: const Color(0xFF8866FF),
              inactiveTrackColor: Colors.white.withValues(alpha: 0.08),
              thumbColor: Colors.white,
              overlayColor: const Color(0xFF8866FF).withValues(alpha: 0.2),
              thumbShape: const RoundSliderThumbShape(enabledThumbRadius: 10),
              trackHeight: 5,
            ),
            child: Slider(
              value: _threshold.toDouble(),
              min: _kThresholdMin.toDouble(),
              max: _kThresholdMax.toDouble(),
              divisions: _kThresholdMax - _kThresholdMin,
              onChanged: connected
                  ? (v) async {
                      final newVal = v.round();
                      setState(() => _threshold = newVal);
                      await _writeThreshold(newVal);
                    }
                  : null,
            ),
          ),

          Row(
            mainAxisAlignment: MainAxisAlignment.spaceBetween,
            children: [
              Text(
                'Más sensible\n${_thresholdToMg(_kThresholdMin).toInt()} mg',
                style: TextStyle(
                    color: Colors.white.withValues(alpha: 0.3),
                    fontSize: 10,
                    height: 1.4),
              ),
              Text(
                'Menos sensible\n${_thresholdToMg(_kThresholdMax).toInt()} mg',
                textAlign: TextAlign.right,
                style: TextStyle(
                    color: Colors.white.withValues(alpha: 0.3),
                    fontSize: 10,
                    height: 1.4),
              ),
            ],
          ),
          const SizedBox(height: 28),

          // Presets
          const _SectionLabel('PRESETS RÁPIDOS'),
          const SizedBox(height: 12),
          _PresetRow(
            current: _threshold,
            presets: const [
              _Preset('Hiper\nsensible', 2, '125 mg'),
              _Preset('Sensible', 4, '250 mg'),
              _Preset('Normal', 8, '500 mg'),
              _Preset('Firme', 12, '750 mg'),
              _Preset('Sacudida', 20, '1.25 G'),
            ],
            onSelect: (reg) async {
              setState(() => _threshold = reg);
              await _writeThreshold(reg);
            },
          ),
          const SizedBox(height: 28),

          // Instructions
          const _SectionLabel('CÓMO PROBAR'),
          const SizedBox(height: 10),
          const _StepItem(
              icon: Icons.watch_rounded, text: 'Ponte el reloj en la muñeca'),
          const _StepItem(
              icon: Icons.screen_rotation_alt_rounded,
              text: 'Levanta la muñeca como para mirar la hora'),
          const _StepItem(
              icon: Icons.visibility_rounded,
              text:
                  'El indicador se pondrá verde si el chip lo detectaría como wake'),
          const _StepItem(
              icon: Icons.adjust_rounded,
              text:
                  'Sube si despierta con vibraciones; baja si no detecta gestos reales'),
        ],
      ),
    );
  }

  // ─────────────────────────────────────────────────────────────────────────
  // AUTO MODE
  // ─────────────────────────────────────────────────────────────────────────

  Widget _buildAutoMode(bool connected) {
    return SingleChildScrollView(
      key: const ValueKey('auto'),
      padding: const EdgeInsets.fromLTRB(20, 28, 20, 40),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          const _InfoBox(
            icon: Icons.auto_awesome_rounded,
            text: 'El reloj mide la aceleración real de tu muñeca durante 5 '
                'gestos y calcula el umbral óptimo automáticamente. '
                'Funciona independientemente del umbral actual — siempre captura los gestos '
                'porque usa el acelerómetro raw, no el hardware de wake-on-motion.',
          ),
          const SizedBox(height: 28),
          _GestureProgress(
            slots: _gestureSlots,
            running: _calibRunning,
            done: _calibDone,
            pulseAnim: _pulseAnim,
            detectAnim: _detectAnim,
          ),
          const SizedBox(height: 32),
          AnimatedSwitcher(
            duration: const Duration(milliseconds: 200),
            child: _calibDone
                ? _buildAutoResult()
                : _calibRunning
                    ? _buildAutoRunning()
                    : _buildAutoIdle(connected),
          ),
          if (!_calibDone && !_calibRunning) ...[
            const SizedBox(height: 32),
            const _SectionLabel('CÓMO FUNCIONA'),
            const SizedBox(height: 10),
            const _StepItem(
                icon: Icons.play_circle_outline_rounded,
                text: 'Pulsa "Iniciar Calibración"'),
            const _StepItem(
                icon: Icons.watch_rounded,
                text:
                    'Levanta la muñeca para mirar la hora — 5 veces, con calma y naturalidad'),
            const _StepItem(
                icon: Icons.check_circle_outline_rounded,
                text: 'Cada gesto capturado se marca en verde en la pantalla'),
            const _StepItem(
                icon: Icons.save_rounded,
                text: 'Al terminar, aplica el umbral calculado al reloj'),
          ],
        ],
      ),
    );
  }

  Widget _buildAutoIdle(bool connected) {
    return SizedBox(
      key: const ValueKey('idle'),
      width: double.infinity,
      child: ElevatedButton.icon(
        onPressed: connected ? _startAutoCalib : null,
        icon: const Icon(Icons.play_arrow_rounded, size: 20),
        label: Text(
            connected ? 'Iniciar Calibración' : 'Conecta el reloj primero'),
        style: ElevatedButton.styleFrom(
          backgroundColor: const Color(0xFF1E1E3A),
          foregroundColor: const Color(0xFFBB99FF),
          disabledBackgroundColor: Colors.white.withValues(alpha: 0.04),
          disabledForegroundColor: Colors.white.withValues(alpha: 0.2),
          padding: const EdgeInsets.symmetric(vertical: 16),
          shape:
              RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
        ),
      ),
    );
  }

  Widget _buildAutoRunning() {
    return Column(
      key: const ValueKey('running'),
      children: [
        Text(
          _calibProgress == 0
              ? 'Levanta la muñeca para mirar la hora...'
              : 'Gesto $_calibProgress/5 capturado — sigue así',
          style: const TextStyle(
            color: Colors.white70,
            fontSize: 15,
            height: 1.4,
          ),
          textAlign: TextAlign.center,
        ),
        const SizedBox(height: 20),
        SizedBox(
          width: double.infinity,
          child: OutlinedButton.icon(
            onPressed: _cancelAutoCalib,
            icon: const Icon(Icons.stop_rounded, size: 18),
            label: const Text('Cancelar'),
            style: OutlinedButton.styleFrom(
              foregroundColor: Colors.white38,
              side: BorderSide(color: Colors.white.withValues(alpha: 0.1)),
              padding: const EdgeInsets.symmetric(vertical: 14),
              shape: RoundedRectangleBorder(
                  borderRadius: BorderRadius.circular(12)),
            ),
          ),
        ),
      ],
    );
  }

  Widget _buildAutoResult() {
    return Column(
      key: const ValueKey('done'),
      crossAxisAlignment: CrossAxisAlignment.center,
      children: [
        const Icon(Icons.check_circle_rounded,
            color: Color(0xFF00CC88), size: 56),
        const SizedBox(height: 12),
        const Text(
          '¡Calibración completa!',
          style: TextStyle(
              color: Colors.white, fontSize: 18, fontWeight: FontWeight.w700),
        ),
        const SizedBox(height: 8),
        if (_calibResultThreshold != null)
          Text(
            'Umbral calculado: ${_thresholdToMg(_calibResultThreshold!).toInt()} mg '
            '(reg 0x${_calibResultThreshold!.toRadixString(16).toUpperCase().padLeft(2, '0')})',
            style: TextStyle(
                color: Colors.white.withValues(alpha: 0.45), fontSize: 13),
            textAlign: TextAlign.center,
          ),
        const SizedBox(height: 8),
        Text(
          'El umbral se calculó analizando el pico mínimo de los 5 gestos '
          'con un margen de seguridad del 80%.',
          style: TextStyle(
              color: Colors.white.withValues(alpha: 0.3),
              fontSize: 11,
              height: 1.5),
          textAlign: TextAlign.center,
        ),
        const SizedBox(height: 24),
        Row(
          children: [
            Expanded(
              child: OutlinedButton(
                onPressed: () => setState(() {
                  _calibDone = false;
                  _gestureSlots = List.filled(5, false);
                }),
                style: OutlinedButton.styleFrom(
                  foregroundColor: Colors.white38,
                  side: BorderSide(color: Colors.white.withValues(alpha: 0.1)),
                  padding: const EdgeInsets.symmetric(vertical: 14),
                  shape: RoundedRectangleBorder(
                      borderRadius: BorderRadius.circular(12)),
                ),
                child: const Text('Repetir'),
              ),
            ),
            const SizedBox(width: 12),
            Expanded(
              flex: 2,
              child: ElevatedButton.icon(
                onPressed:
                    _calibResultThreshold != null ? _applyAutoResult : null,
                icon: const Icon(Icons.save_rounded, size: 18),
                label: const Text('Aplicar al reloj'),
                style: ElevatedButton.styleFrom(
                  backgroundColor: const Color(0xFF00AA66),
                  foregroundColor: Colors.white,
                  padding: const EdgeInsets.symmetric(vertical: 14),
                  shape: RoundedRectangleBorder(
                      borderRadius: BorderRadius.circular(12)),
                ),
              ),
            ),
          ],
        ),
      ],
    );
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Sub-widgets
// ─────────────────────────────────────────────────────────────────────────────

class _ModeTabBar extends StatelessWidget {
  final _CalibMode selected;
  final ValueChanged<_CalibMode> onChanged;
  const _ModeTabBar({required this.selected, required this.onChanged});

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.all(4),
      decoration: BoxDecoration(
        color: Colors.white.withValues(alpha: 0.06),
        borderRadius: BorderRadius.circular(12),
      ),
      child: Row(
        children: [
          _tab(context, _CalibMode.manual, Icons.tune_rounded, 'Manual'),
          _tab(context, _CalibMode.auto, Icons.auto_awesome_rounded,
              'Automático'),
        ],
      ),
    );
  }

  Widget _tab(
      BuildContext context, _CalibMode mode, IconData icon, String label) {
    final active = selected == mode;
    return Expanded(
      child: GestureDetector(
        onTap: () => onChanged(mode),
        child: AnimatedContainer(
          duration: const Duration(milliseconds: 200),
          padding: const EdgeInsets.symmetric(vertical: 10),
          decoration: BoxDecoration(
            color: active
                ? const Color(0xFF8866FF).withValues(alpha: 0.2)
                : Colors.transparent,
            borderRadius: BorderRadius.circular(9),
            border: active
                ? Border.all(
                    color: const Color(0xFF8866FF).withValues(alpha: 0.4))
                : null,
          ),
          child: Row(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              Icon(icon,
                  size: 16,
                  color: active
                      ? const Color(0xFFBB99FF)
                      : Colors.white.withValues(alpha: 0.35)),
              const SizedBox(width: 6),
              Text(
                label,
                style: TextStyle(
                  color: active
                      ? const Color(0xFFBB99FF)
                      : Colors.white.withValues(alpha: 0.35),
                  fontWeight: active ? FontWeight.w600 : FontWeight.w400,
                  fontSize: 13,
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

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
        final baseColor = isDetected
            ? const Color(0xFF00CC88)
            : Colors.white.withValues(alpha: 0.06);
        final glowColor = isDetected
            ? Color.lerp(
                const Color(0xFF00CC88), const Color(0xFF00FF99), flash)!
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
                    isDetected
                        ? Icons.sensors_rounded
                        : Icons.sensors_off_rounded,
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
                  child: Text(isDetected ? 'DETECTADO' : 'SIN DETECCIÓN'),
                ),
                const SizedBox(height: 5),
                Text(
                  isDetected
                      ? 'El chip activaría el wake-up'
                      : 'Mueve el reloj como para ver la hora',
                  style: TextStyle(
                      color: textColor.withValues(alpha: 0.6), fontSize: 11),
                ),
                if (totalDetections > 0) ...[
                  const SizedBox(height: 10),
                  Text(
                    '$totalDetections detecciones en esta sesión',
                    style: TextStyle(
                        color: Colors.white.withValues(alpha: 0.18),
                        fontSize: 10),
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

class _GestureProgress extends StatelessWidget {
  final List<bool> slots;
  final bool running;
  final bool done;
  final Animation<double> pulseAnim;
  final Animation<double> detectAnim;

  const _GestureProgress({
    required this.slots,
    required this.running,
    required this.done,
    required this.pulseAnim,
    required this.detectAnim,
  });

  @override
  Widget build(BuildContext context) {
    return AnimatedBuilder(
      animation: Listenable.merge([pulseAnim, detectAnim]),
      builder: (context, _) {
        final capturedCount = slots.where((s) => s).length;
        return Row(
          mainAxisAlignment: MainAxisAlignment.center,
          children: List.generate(5, (i) {
            final captured = slots[i];
            final isNext = running && !done && i == capturedCount;
            final pulse = isNext ? pulseAnim.value : 0.0;

            Color color;
            double size;
            Widget childWidget;

            if (captured) {
              color = const Color(0xFF00CC88);
              size = 52;
              childWidget = const Icon(Icons.check_rounded,
                  color: Colors.white, size: 22);
            } else if (isNext) {
              color = Color.lerp(
                const Color(0xFF8866FF).withValues(alpha: 0.3),
                const Color(0xFF8866FF).withValues(alpha: 0.75),
                pulse,
              )!;
              size = 52.0 + pulse * 5;
              childWidget = const Icon(Icons.watch_rounded,
                  color: Colors.white70, size: 20);
            } else {
              color = Colors.white.withValues(alpha: 0.06);
              size = 52;
              childWidget = Text(
                '${i + 1}',
                style: TextStyle(
                  color: Colors.white.withValues(alpha: 0.2),
                  fontWeight: FontWeight.w700,
                  fontSize: 16,
                ),
              );
            }

            return Padding(
              padding: const EdgeInsets.symmetric(horizontal: 5),
              child: AnimatedContainer(
                duration: const Duration(milliseconds: 250),
                width: size,
                height: size,
                decoration: BoxDecoration(
                  color: color,
                  shape: BoxShape.circle,
                  boxShadow: (captured || isNext)
                      ? [
                          BoxShadow(
                            color: color.withValues(alpha: 0.35 + 0.2 * pulse),
                            blurRadius: 14,
                            spreadRadius: 1,
                          )
                        ]
                      : null,
                ),
                child: Center(child: childWidget),
              ),
            );
          }),
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
          Icon(icon, color: const Color(0xFFBB99FF), size: 18),
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
                size: 15,
                color: const Color(0xFF8866FF).withValues(alpha: 0.55)),
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
  final int reg;
  final String mg;
  const _Preset(this.label, this.reg, this.mg);
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
        final active = current == p.reg;
        return GestureDetector(
          onTap: () => onSelect(p.reg),
          child: AnimatedContainer(
            duration: const Duration(milliseconds: 180),
            padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
            decoration: BoxDecoration(
              color: active
                  ? const Color(0xFF8866FF).withValues(alpha: 0.18)
                  : Colors.white.withValues(alpha: 0.04),
              borderRadius: BorderRadius.circular(10),
              border: Border.all(
                color: active
                    ? const Color(0xFF8866FF).withValues(alpha: 0.5)
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
                        ? const Color(0xFFBB99FF)
                        : Colors.white.withValues(alpha: 0.4),
                    fontSize: 11,
                    fontWeight: FontWeight.w600,
                    height: 1.3,
                  ),
                ),
                const SizedBox(height: 2),
                Text(
                  p.mg,
                  style: TextStyle(
                    color: active
                        ? const Color(0xFFBB99FF).withValues(alpha: 0.55)
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
