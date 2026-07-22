import 'dart:async';
import 'dart:math' as math;
import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import 'package:shared_preferences/shared_preferences.dart';
import 'package:flutter_foreground_task/flutter_foreground_task.dart';
import 'package:flutter_colorpicker/flutter_colorpicker.dart';

import '../main.dart';
import '../models/config_model.dart';
import '../repositories/partner_repository.dart';
import '../services/ble_service.dart';
import '../services/sync_service.dart';
import '../widgets/watch_preview_widget.dart';
import 'wake_calibration_screen.dart';
import 'wrist_flick_calibration_screen.dart';
import 'compass_diagnostic_screen.dart';

/// Pantalla principal de Configuración (Dashboard por Categorías).
class SettingsScreen extends StatefulWidget {
  const SettingsScreen({super.key});

  @override
  State<SettingsScreen> createState() => _SettingsScreenState();
}

class _SettingsScreenState extends State<SettingsScreen> {
  late WatchConfig _config;

  @override
  void initState() {
    super.initState();
    final repo = context.read<PartnerRepository>();
    _config = repo.config ?? WatchConfig.defaultFor(repo.myUserId);
  }

  void _onConfigUpdated(WatchConfig newConfig) {
    setState(() {
      _config = newConfig;
    });
  }

  @override
  Widget build(BuildContext context) {
    final repo = context.watch<PartnerRepository>();
    if (repo.config != null && repo.config != _config) {
      _config = repo.config!;
    }

    return Scaffold(
      backgroundColor: const Color(0xFF0A0A1A),
      appBar: AppBar(
        backgroundColor: Colors.transparent,
        elevation: 0,
        title: const Text(
          'Configuración',
          style: TextStyle(fontWeight: FontWeight.w700, letterSpacing: -0.5),
        ),
      ),
      body: ListView(
        padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 8),
        children: [
          // ── Preview interactivo ───────────────────────────────────────
          Center(
            child: WatchPreviewWidget(
              mode: WatchPreviewMode.clock,
              config: _config,
              isConnected: true,
              size: 150,
            ),
          ),
          const SizedBox(height: 24),

          // ── Categorías principales ─────────────────────────────────────
          _buildCategoryCard(
            context,
            icon: Icons.palette_rounded,
            iconColor: const Color(0xFFFF5588),
            title: 'Estilo y Esferas',
            subtitle: 'Preview, colores de manecillas y brillo global',
            onTap: () => Navigator.push(
              context,
              MaterialPageRoute(
                builder: (_) => StyleSettingsPage(
                  initialConfig: _config,
                  onConfigSaved: _onConfigUpdated,
                ),
              ),
            ),
          ),

          _buildCategoryCard(
            context,
            icon: Icons.vibration_rounded,
            iconColor: const Color(0xFF00FFFF),
            title: 'Toques y Hápticos',
            subtitle: 'Toques de pareja, colores TX/RX y brillo háptico',
            onTap: () => Navigator.push(
              context,
              MaterialPageRoute(
                builder: (_) => InteractionsSettingsPage(
                  initialConfig: _config,
                  onConfigSaved: _onConfigUpdated,
                ),
              ),
            ),
          ),

          _buildCategoryCard(
            context,
            icon: Icons.back_hand_rounded,
            iconColor: const Color(0xFFFFBB00),
            title: 'Gestos y Sensores',
            subtitle: 'Sensibilidad de giros, encendido y calibraciones',
            onTap: () => Navigator.push(
              context,
              MaterialPageRoute(
                builder: (_) => GesturesSettingsPage(
                  initialConfig: _config,
                  onConfigSaved: _onConfigUpdated,
                ),
              ),
            ),
          ),

          _buildCategoryCard(
            context,
            icon: Icons.explore_rounded,
            iconColor: const Color(0xFF00FF88),
            title: 'Radar y Distancias',
            subtitle: 'Aguja de radar, colores por zonas y umbrales en km',
            onTap: () => Navigator.push(
              context,
              MaterialPageRoute(
                builder: (_) => RadarSettingsPage(
                  initialConfig: _config,
                  onConfigSaved: _onConfigUpdated,
                ),
              ),
            ),
          ),

          _buildCategoryCard(
            context,
            icon: Icons.tune_rounded,
            iconColor: const Color(0xFF4488FF),
            title: 'Sistema y Diagnóstico',
            subtitle: 'Timeouts, batería baja, GPS polling y firmware OTA',
            onTap: () => Navigator.push(
              context,
              MaterialPageRoute(
                builder: (_) => SystemSettingsPage(
                  initialConfig: _config,
                  onConfigSaved: _onConfigUpdated,
                ),
              ),
            ),
          ),

          const SizedBox(height: 24),
        ],
      ),
    );
  }

  Widget _buildCategoryCard(
    BuildContext context, {
    required IconData icon,
    required Color iconColor,
    required String title,
    required String subtitle,
    required VoidCallback onTap,
  }) {
    return Container(
      margin: const EdgeInsets.only(bottom: 14),
      decoration: BoxDecoration(
        color: const Color(0xFF141428),
        borderRadius: BorderRadius.circular(16),
        border: Border.all(color: Colors.white.withValues(alpha: 0.06)),
      ),
      child: ListTile(
        contentPadding: const EdgeInsets.symmetric(horizontal: 16, vertical: 10),
        leading: Container(
          padding: const EdgeInsets.all(10),
          decoration: BoxDecoration(
            color: iconColor.withValues(alpha: 0.12),
            borderRadius: BorderRadius.circular(12),
          ),
          child: Icon(icon, color: iconColor, size: 24),
        ),
        title: Text(
          title,
          style: const TextStyle(fontWeight: FontWeight.w700, fontSize: 15, color: Colors.white),
        ),
        subtitle: Padding(
          padding: const EdgeInsets.only(top: 4),
          child: Text(
            subtitle,
            style: TextStyle(fontSize: 12, color: Colors.white.withValues(alpha: 0.4), height: 1.3),
          ),
        ),
        trailing: const Icon(Icons.arrow_forward_ios_rounded, size: 16, color: Colors.white30),
        onTap: onTap,
      ),
    );
  }
}

// ============================================================================
// SUBCATEGORÍA 1: ESTILO Y ESFERAS
// ============================================================================

class StyleSettingsPage extends StatefulWidget {
  final WatchConfig initialConfig;
  final ValueChanged<WatchConfig> onConfigSaved;

  const StyleSettingsPage({
    super.key,
    required this.initialConfig,
    required this.onConfigSaved,
  });

  @override
  State<StyleSettingsPage> createState() => _StyleSettingsPageState();
}

class _StyleSettingsPageState extends State<StyleSettingsPage> {
  late WatchConfig _config;
  bool _hasChanges = false;
  bool _isSaving = false;

  @override
  void initState() {
    super.initState();
    _config = widget.initialConfig;
  }

  void _updateConfig(WatchConfig Function(WatchConfig) updater) {
    setState(() {
      _config = updater(_config);
      _hasChanges = true;
    });
  }

  Future<void> _saveConfig() async {
    setState(() => _isSaving = true);
    final repo = context.read<PartnerRepository>();
    final bleService = context.read<BleService>();
    try {
      await repo.saveConfig(_config);
      await bleService.writeConfig(_config);
      widget.onConfigSaved(_config);
      if (!mounted) return;
      setState(() {
        _hasChanges = false;
        _isSaving = false;
      });
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
          content: Text('Configuración guardada y sincronizada'),
          backgroundColor: Color(0xFF1A2A3A),
          behavior: SnackBarBehavior.floating,
        ),
      );
    } catch (e) {
      if (!mounted) return;
      setState(() => _isSaving = false);
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('Error al guardar: $e'), backgroundColor: Colors.red),
      );
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: const Color(0xFF0A0A1A),
      appBar: AppBar(
        backgroundColor: Colors.transparent,
        title: const Text('Estilo y Esferas', style: TextStyle(fontWeight: FontWeight.w700)),
        actions: [
          if (_hasChanges)
            TextButton.icon(
              onPressed: _isSaving ? null : _saveConfig,
              icon: _isSaving
                  ? const SizedBox(width: 14, height: 14, child: CircularProgressIndicator(strokeWidth: 2, color: Color(0xFF4488FF)))
                  : const Icon(Icons.check_rounded, color: Color(0xFF4488FF), size: 18),
              label: const Text('Guardar', style: TextStyle(color: Color(0xFF4488FF), fontWeight: FontWeight.w700)),
            ),
        ],
      ),
      body: ListView(
        padding: const EdgeInsets.all(20),
        children: [
          Center(
            child: WatchPreviewWidget(
              mode: WatchPreviewMode.clock,
              config: _config,
              isConnected: true,
              size: 180,
            ),
          ),
          const SizedBox(height: 24),

          _buildSection('Brillo Global', [
            _buildSliderTile(
              'Brillo global',
              '${_config.brightnessPercent}%',
              _config.brightnessPercent.toDouble(),
              0,
              100,
              (value) => _updateConfig((c) => c.copyWith(brightnessPercent: value.round())),
              activeColor: const Color(0xFFFFCC00),
            ),
            _buildSwitchTile(
              'Brillo logarítmico',
              'Curva gamma para percepción lineal de brillo',
              _config.logarithmicBrightness,
              (value) => _updateConfig((c) => c.copyWith(logarithmicBrightness: value)),
            ),
          ]),

          _buildSection('Colores — Conectado', [
            _buildColorTile(
              context,
              'Horas',
              _config.hoursConnectedColor,
              _config.colorHoursConnected,
              (hex) => _updateConfig((c) => c.copyWith(colorHoursConnected: hex)),
            ),
            _buildColorTile(
              context,
              'Minutos',
              _config.minutesConnectedColor,
              _config.colorMinutesConnected,
              (hex) => _updateConfig((c) => c.copyWith(colorMinutesConnected: hex)),
            ),
            _buildColorTile(
              context,
              'Segundos',
              _config.secondsConnectedColor,
              _config.colorSecondsConnected,
              (hex) => _updateConfig((c) => c.copyWith(colorSecondsConnected: hex)),
            ),
          ]),

          _buildSection('Colores — Desconectado', [
            _buildColorTile(
              context,
              'Horas',
              _config.hoursDiscColor,
              _config.colorHoursDisc,
              (hex) => _updateConfig((c) => c.copyWith(colorHoursDisc: hex)),
            ),
            _buildColorTile(
              context,
              'Minutos',
              _config.minutesDiscColor,
              _config.colorMinutesDisc,
              (hex) => _updateConfig((c) => c.copyWith(colorMinutesDisc: hex)),
            ),
            _buildColorTile(
              context,
              'Segundos',
              _config.secondsDiscColor,
              _config.colorSecondsDisc,
              (hex) => _updateConfig((c) => c.copyWith(colorSecondsDisc: hex)),
            ),
          ]),
        ],
      ),
    );
  }
}

// ============================================================================
// SUBCATEGORÍA 2: TOQUES Y FEEDBACK VISUAL
// ============================================================================

class InteractionsSettingsPage extends StatefulWidget {
  final WatchConfig initialConfig;
  final ValueChanged<WatchConfig> onConfigSaved;

  const InteractionsSettingsPage({
    super.key,
    required this.initialConfig,
    required this.onConfigSaved,
  });

  @override
  State<InteractionsSettingsPage> createState() => _InteractionsSettingsPageState();
}

class _InteractionsSettingsPageState extends State<InteractionsSettingsPage> {
  late WatchConfig _config;
  bool _hasChanges = false;
  bool _isSaving = false;
  bool _isHapticSending = false;

  @override
  void initState() {
    super.initState();
    _config = widget.initialConfig;
  }

  void _updateConfig(WatchConfig Function(WatchConfig) updater) {
    setState(() {
      _config = updater(_config);
      _hasChanges = true;
    });
  }

  Future<void> _saveConfig() async {
    setState(() => _isSaving = true);
    final repo = context.read<PartnerRepository>();
    final bleService = context.read<BleService>();
    try {
      await repo.saveConfig(_config);
      await bleService.writeConfig(_config);
      widget.onConfigSaved(_config);
      if (!mounted) return;
      setState(() {
        _hasChanges = false;
        _isSaving = false;
      });
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
          content: Text('Configuración guardada y sincronizada'),
          backgroundColor: Color(0xFF1A2A3A),
          behavior: SnackBarBehavior.floating,
        ),
      );
    } catch (e) {
      if (!mounted) return;
      setState(() => _isSaving = false);
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('Error al guardar: $e'), backgroundColor: Colors.red),
      );
    }
  }

  @override
  Widget build(BuildContext context) {
    final bleService = context.watch<BleService>();
    final connected = bleService.connectionState == BleConnectionState.connected;

    return Scaffold(
      backgroundColor: const Color(0xFF0A0A1A),
      appBar: AppBar(
        backgroundColor: Colors.transparent,
        title: const Text('Toques y Feedback Visual', style: TextStyle(fontWeight: FontWeight.w700)),
        actions: [
          if (_hasChanges)
            TextButton.icon(
              onPressed: _isSaving ? null : _saveConfig,
              icon: _isSaving
                  ? const SizedBox(width: 14, height: 14, child: CircularProgressIndicator(strokeWidth: 2, color: Color(0xFF4488FF)))
                  : const Icon(Icons.check_rounded, color: Color(0xFF4488FF), size: 18),
              label: const Text('Guardar', style: TextStyle(color: Color(0xFF4488FF), fontWeight: FontWeight.w700)),
            ),
        ],
      ),
      body: ListView(
        padding: const EdgeInsets.all(20),
        children: [
          // ── Toque Háptico Enviado ──────────────────────────────────
          _buildSection('Toque Háptico Enviado (TX)', [
            _buildColorTile(
              context,
              'Color al enviar toque',
              _parseColor(_config.colorHapticTx),
              _config.colorHapticTx,
              (hex) => _updateConfig((c) => c.copyWith(colorHapticTx: hex)),
            ),
            _buildSliderTile(
              'Brillo al enviar',
              '${_config.brightnessHapticTx}%',
              _config.brightnessHapticTx.toDouble(),
              10,
              100,
              (value) => _updateConfig((c) => c.copyWith(brightnessHapticTx: value.round())),
              activeColor: const Color(0xFF66CCFF),
            ),
          ]),

          // ── Toque Háptico Recibido ──────────────────────────────────
          _buildSection('Toque Háptico Recibido (RX)', [
            _buildColorTile(
              context,
              'Color al recibir toque',
              _parseColor(_config.colorHapticRx),
              _config.colorHapticRx,
              (hex) => _updateConfig((c) => c.copyWith(colorHapticRx: hex)),
            ),
            _buildSliderTile(
              'Brillo al recibir',
              '${_config.brightnessHapticRx}%',
              _config.brightnessHapticRx.toDouble(),
              10,
              100,
              (value) => _updateConfig((c) => c.copyWith(brightnessHapticRx: value.round())),
              activeColor: const Color(0xFFFF6699),
            ),
          ]),

          // ── Notificaciones y Prueba ─────────────────────────────────
          _buildSection('Configuración de Vibración', [
            _buildDropdownTile<String>(
              'Quién vibra al tocar',
              'Selecciona qué relojes vibran con un doble toque',
              _config.hapticPattern == 'partner' ? 'partner' : 'both',
              const {
                'both': 'Ambos relojes',
                'partner': 'Solo el de tu pareja',
              },
              (value) {
                if (value != null) {
                  _updateConfig((c) => c.copyWith(hapticPattern: value));
                }
              },
            ),
            Padding(
              padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
              child: SizedBox(
                width: double.infinity,
                child: ElevatedButton.icon(
                  onPressed: _isHapticSending
                      ? null
                      : () async {
                          setState(() => _isHapticSending = true);
                          try {
                            final syncService = context.read<SyncService>();
                            if (connected) {
                              bleService.sendHapticCommand().catchError((_) {});
                            }
                            await syncService.sendHapticEvent();
                            if (!mounted) return;
                            ScaffoldMessenger.of(context).showSnackBar(
                              SnackBar(
                                content: const Text('Toque enviado a tu pareja'),
                                backgroundColor: const Color(0xFF1E2D2A),
                                behavior: SnackBarBehavior.floating,
                                shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(8)),
                              ),
                            );
                          } catch (e) {
                            if (!mounted) return;
                            ScaffoldMessenger.of(context).showSnackBar(
                              SnackBar(
                                content: Text('Error al enviar toque: $e'),
                                backgroundColor: const Color(0xFF3E1C1C),
                                behavior: SnackBarBehavior.floating,
                                shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(8)),
                              ),
                            );
                          } finally {
                            if (mounted) setState(() => _isHapticSending = false);
                          }
                        },
                  icon: _isHapticSending
                      ? const SizedBox(width: 18, height: 18, child: CircularProgressIndicator(strokeWidth: 2))
                      : const Icon(Icons.vibration_rounded, size: 18),
                  label: const Text('Probar vibración (Enviar toque)'),
                  style: ElevatedButton.styleFrom(
                    backgroundColor: const Color(0xFF1C2E2A),
                    foregroundColor: const Color(0xFF88FFBB),
                    padding: const EdgeInsets.symmetric(vertical: 14),
                    shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(10)),
                  ),
                ),
              ),
            ),
          ]),
        ],
      ),
    );
  }
}

// ============================================================================
// SUBCATEGORÍA 3: GESTOS Y SENSORES
// ============================================================================

class GesturesSettingsPage extends StatefulWidget {
  final WatchConfig initialConfig;
  final ValueChanged<WatchConfig> onConfigSaved;

  const GesturesSettingsPage({
    super.key,
    required this.initialConfig,
    required this.onConfigSaved,
  });

  @override
  State<GesturesSettingsPage> createState() => _GesturesSettingsPageState();
}

class _GesturesSettingsPageState extends State<GesturesSettingsPage> {
  late WatchConfig _config;
  bool _hasChanges = false;
  bool _isSaving = false;

  @override
  void initState() {
    super.initState();
    _config = widget.initialConfig;
  }

  void _updateConfig(WatchConfig Function(WatchConfig) updater) {
    setState(() {
      _config = updater(_config);
      _hasChanges = true;
    });
  }

  Future<void> _saveConfig() async {
    setState(() => _isSaving = true);
    final repo = context.read<PartnerRepository>();
    final bleService = context.read<BleService>();
    try {
      await repo.saveConfig(_config);
      await bleService.writeConfig(_config);
      widget.onConfigSaved(_config);
      if (!mounted) return;
      setState(() {
        _hasChanges = false;
        _isSaving = false;
      });
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
          content: Text('Configuración guardada y sincronizada'),
          backgroundColor: Color(0xFF1A2A3A),
          behavior: SnackBarBehavior.floating,
        ),
      );
    } catch (e) {
      if (!mounted) return;
      setState(() => _isSaving = false);
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('Error al guardar: $e'), backgroundColor: Colors.red),
      );
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: const Color(0xFF0A0A1A),
      appBar: AppBar(
        backgroundColor: Colors.transparent,
        title: const Text('Gestos y Sensores', style: TextStyle(fontWeight: FontWeight.w700)),
        actions: [
          if (_hasChanges)
            TextButton.icon(
              onPressed: _isSaving ? null : _saveConfig,
              icon: _isSaving
                  ? const SizedBox(width: 14, height: 14, child: CircularProgressIndicator(strokeWidth: 2, color: Color(0xFF4488FF)))
                  : const Icon(Icons.check_rounded, color: Color(0xFF4488FF), size: 18),
              label: const Text('Guardar', style: TextStyle(color: Color(0xFF4488FF), fontWeight: FontWeight.w700)),
            ),
        ],
      ),
      body: ListView(
        padding: const EdgeInsets.all(20),
        children: [
          _buildSection('Giro de Muñeca (Wrist Flick)', [
            _buildSliderTile(
              'Sensibilidad de giro',
              '${_config.gyroThreshold} dps',
              _config.gyroThreshold.toDouble(),
              100,
              2000,
              (value) => _updateConfig((c) => c.copyWith(gyroThreshold: value.round())),
              activeColor: const Color(0xFF00CC88),
            ),
            _buildSliderTile(
              'Ventana de doble giro',
              '${_config.doubleFlickWindowMs} ms',
              _config.doubleFlickWindowMs.toDouble(),
              400,
              1200,
              (value) => _updateConfig((c) => c.copyWith(doubleFlickWindowMs: value.round())),
              activeColor: const Color(0xFF00FFCC),
            ),
            _buildSliderTile(
              'Ventana de triple giro',
              '${_config.tripleFlickWindowMs} ms',
              _config.tripleFlickWindowMs.toDouble(),
              400,
              2000,
              (value) => _updateConfig((c) => c.copyWith(tripleFlickWindowMs: value.round())),
              activeColor: const Color(0xFF00DDDD),
            ),
            Padding(
              padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
              child: SizedBox(
                width: double.infinity,
                child: ElevatedButton.icon(
                  onPressed: () => Navigator.push(
                    context,
                    MaterialPageRoute(builder: (_) => const WristFlickCalibrationScreen()),
                  ),
                  icon: const Icon(Icons.sync_rounded, size: 18),
                  label: const Text('Calibrar giros en vivo'),
                  style: ElevatedButton.styleFrom(
                    backgroundColor: const Color(0xFF1C2E24),
                    foregroundColor: const Color(0xFF88FFBB),
                    padding: const EdgeInsets.symmetric(vertical: 14),
                    shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(10)),
                  ),
                ),
              ),
            ),
          ]),

          _buildSection('Rise-to-Wake (Encendido por Movimiento)', [
            _buildSliderTile(
              'Sensibilidad de encendido',
              '${(_config.wakeThreshold * 62.5).toInt()} mg',
              _config.wakeThreshold.toDouble(),
              1,
              63,
              (value) => _updateConfig((c) => c.copyWith(wakeThreshold: value.round())),
              activeColor: const Color(0xFF8866FF),
            ),
            Padding(
              padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
              child: SizedBox(
                width: double.infinity,
                child: ElevatedButton.icon(
                  onPressed: () async {
                    await Navigator.push(
                      context,
                      MaterialPageRoute(builder: (_) => const WakeCalibrationScreen()),
                    );
                  },
                  icon: const Icon(Icons.tune_rounded, size: 18),
                  label: const Text('Calibración avanzada de encendido'),
                  style: ElevatedButton.styleFrom(
                    backgroundColor: const Color(0xFF1E1C2E),
                    foregroundColor: const Color(0xFFBB88FF),
                    padding: const EdgeInsets.symmetric(vertical: 14),
                    shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(10)),
                  ),
                ),
              ),
            ),
          ]),
        ],
      ),
    );
  }
}

// ============================================================================
// SUBCATEGORÍA 4: RADAR Y DISTANCIAS
// ============================================================================

class RadarSettingsPage extends StatefulWidget {
  final WatchConfig initialConfig;
  final ValueChanged<WatchConfig> onConfigSaved;

  const RadarSettingsPage({
    super.key,
    required this.initialConfig,
    required this.onConfigSaved,
  });

  @override
  State<RadarSettingsPage> createState() => _RadarSettingsPageState();
}

class _RadarSettingsPageState extends State<RadarSettingsPage> {
  late WatchConfig _config;
  bool _hasChanges = false;
  bool _isSaving = false;

  @override
  void initState() {
    super.initState();
    _config = widget.initialConfig;
  }

  void _updateConfig(WatchConfig Function(WatchConfig) updater) {
    setState(() {
      _config = updater(_config);
      _hasChanges = true;
    });
  }

  Future<void> _saveConfig() async {
    setState(() => _isSaving = true);
    final repo = context.read<PartnerRepository>();
    final bleService = context.read<BleService>();
    try {
      await repo.saveConfig(_config);
      await bleService.writeConfig(_config);
      widget.onConfigSaved(_config);
      if (!mounted) return;
      setState(() {
        _hasChanges = false;
        _isSaving = false;
      });
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
          content: Text('Configuración guardada y sincronizada'),
          backgroundColor: Color(0xFF1A2A3A),
          behavior: SnackBarBehavior.floating,
        ),
      );
    } catch (e) {
      if (!mounted) return;
      setState(() => _isSaving = false);
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('Error al guardar: $e'), backgroundColor: Colors.red),
      );
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: const Color(0xFF0A0A1A),
      appBar: AppBar(
        backgroundColor: Colors.transparent,
        title: const Text('Radar y Distancias', style: TextStyle(fontWeight: FontWeight.w700)),
        actions: [
          if (_hasChanges)
            TextButton.icon(
              onPressed: _isSaving ? null : _saveConfig,
              icon: _isSaving
                  ? const SizedBox(width: 14, height: 14, child: CircularProgressIndicator(strokeWidth: 2, color: Color(0xFF4488FF)))
                  : const Icon(Icons.check_rounded, color: Color(0xFF4488FF), size: 18),
              label: const Text('Guardar', style: TextStyle(color: Color(0xFF4488FF), fontWeight: FontWeight.w700)),
            ),
        ],
      ),
      body: ListView(
        padding: const EdgeInsets.all(20),
        children: [
          Center(
            child: WatchPreviewWidget(
              mode: WatchPreviewMode.radar,
              config: _config,
              isConnected: true,
              size: 170,
            ),
          ),
          const SizedBox(height: 24),

          _buildSection('Color del Radar', [
            _buildColorTile(
              context,
              'Aguja de brújula/radar',
              _parseColor(_config.colorRadar),
              _config.colorRadar,
              (hex) => _updateConfig((c) => c.copyWith(colorRadar: hex)),
            ),
          ]),

          _buildSection('Colores por Zonas de Distancia', [
            _buildColorTile(
              context,
              'Cercano (<${_config.distThresh1Km} km)',
              _parseColor(_config.colorDistanceNear),
              _config.colorDistanceNear,
              (hex) => _updateConfig((c) => c.copyWith(colorDistanceNear: hex)),
            ),
            _buildColorTile(
              context,
              'Provincia (${_config.distThresh1Km}-${_config.distThresh2Km} km)',
              _parseColor(_config.colorDistanceProv),
              _config.colorDistanceProv,
              (hex) => _updateConfig((c) => c.copyWith(colorDistanceProv: hex)),
            ),
            _buildColorTile(
              context,
              'Lejos (${_config.distThresh2Km}-${_config.distThresh3Km} km)',
              _parseColor(_config.colorDistanceFar),
              _config.colorDistanceFar,
              (hex) => _updateConfig((c) => c.copyWith(colorDistanceFar: hex)),
            ),
            _buildColorTile(
              context,
              'Muy lejos (${_config.distThresh3Km}-${_config.distThresh4Km} km)',
              _parseColor(_config.colorDistanceVFar),
              _config.colorDistanceVFar,
              (hex) => _updateConfig((c) => c.copyWith(colorDistanceVFar: hex)),
            ),
            _buildColorTile(
              context,
              'Extremo (${_config.distThresh4Km}-${_config.distThreshMaxKm} km)',
              _parseColor(_config.colorDistanceExtr),
              _config.colorDistanceExtr,
              (hex) => _updateConfig((c) => c.copyWith(colorDistanceExtr: hex)),
            ),
          ]),

          _buildSection('Umbrales de Distancia (km)', [
            _buildSliderTile(
              'Zona Cercana',
              '${_config.distThresh1Km} km',
              _config.distThresh1Km.toDouble(),
              1,
              50,
              (value) => _updateConfig((c) => c.copyWith(distThresh1Km: value.round())),
              activeColor: const Color(0xFF0080FF),
            ),
            _buildSliderTile(
              'Zona Provincia',
              '${_config.distThresh2Km} km',
              _config.distThresh2Km.toDouble(),
              10,
              150,
              (value) => _updateConfig((c) => c.copyWith(distThresh2Km: value.round())),
              activeColor: const Color(0xFF00CC44),
            ),
            _buildSliderTile(
              'Zona Lejana',
              '${_config.distThresh3Km} km',
              _config.distThresh3Km.toDouble(),
              50,
              300,
              (value) => _updateConfig((c) => c.copyWith(distThresh3Km: value.round())),
              activeColor: const Color(0xFFFFCC00),
            ),
            _buildSliderTile(
              'Zona Muy Lejana',
              '${_config.distThresh4Km} km',
              _config.distThresh4Km.toDouble(),
              100,
              600,
              (value) => _updateConfig((c) => c.copyWith(distThresh4Km: value.round())),
              activeColor: const Color(0xFFFF6600),
            ),
            _buildSliderTile(
              'Zona Extrema (Máximo)',
              '${_config.distThreshMaxKm} km',
              _config.distThreshMaxKm.toDouble(),
              200,
              2000,
              (value) => _updateConfig((c) => c.copyWith(distThreshMaxKm: value.round())),
              activeColor: const Color(0xFFFF0000),
            ),
          ]),
        ],
      ),
    );
  }
}

// ============================================================================
// SUBCATEGORÍA 5: SISTEMA Y DIAGNÓSTICO
// ============================================================================

class SystemSettingsPage extends StatefulWidget {
  final WatchConfig initialConfig;
  final ValueChanged<WatchConfig> onConfigSaved;

  const SystemSettingsPage({
    super.key,
    required this.initialConfig,
    required this.onConfigSaved,
  });

  @override
  State<SystemSettingsPage> createState() => _SystemSettingsPageState();
}

class _SystemSettingsPageState extends State<SystemSettingsPage> {
  late WatchConfig _config;
  bool _hasChanges = false;
  bool _isSaving = false;

  @override
  void initState() {
    super.initState();
    _config = widget.initialConfig;
  }

  void _updateConfig(WatchConfig Function(WatchConfig) updater) {
    setState(() {
      _config = updater(_config);
      _hasChanges = true;
    });
  }

  Future<void> _saveConfig() async {
    setState(() => _isSaving = true);
    final repo = context.read<PartnerRepository>();
    final bleService = context.read<BleService>();
    try {
      await repo.saveConfig(_config);
      await bleService.writeConfig(_config);
      widget.onConfigSaved(_config);
      if (!mounted) return;
      setState(() {
        _hasChanges = false;
        _isSaving = false;
      });
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
          content: Text('Configuración guardada y sincronizada'),
          backgroundColor: Color(0xFF1A2A3A),
          behavior: SnackBarBehavior.floating,
        ),
      );
    } catch (e) {
      if (!mounted) return;
      setState(() => _isSaving = false);
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('Error al guardar: $e'), backgroundColor: Colors.red),
      );
    }
  }

  @override
  Widget build(BuildContext context) {
    final bleService = context.watch<BleService>();
    final connected = bleService.connectionState == BleConnectionState.connected;

    return Scaffold(
      backgroundColor: const Color(0xFF0A0A1A),
      appBar: AppBar(
        backgroundColor: Colors.transparent,
        title: const Text('Sistema y Diagnóstico', style: TextStyle(fontWeight: FontWeight.w700)),
        actions: [
          if (_hasChanges)
            TextButton.icon(
              onPressed: _isSaving ? null : _saveConfig,
              icon: _isSaving
                  ? const SizedBox(width: 14, height: 14, child: CircularProgressIndicator(strokeWidth: 2, color: Color(0xFF4488FF)))
                  : const Icon(Icons.check_rounded, color: Color(0xFF4488FF), size: 18),
              label: const Text('Guardar', style: TextStyle(color: Color(0xFF4488FF), fontWeight: FontWeight.w700)),
            ),
        ],
      ),
      body: ListView(
        padding: const EdgeInsets.all(20),
        children: [
          _buildSection('Temporizadores de Reposo', [
            _buildSliderTile(
              'Timeout reloj → sleep',
              '${_config.clockTimeoutS}s',
              _config.clockTimeoutS.toDouble(),
              3,
              30,
              (value) => _updateConfig((c) => c.copyWith(clockTimeoutS: value.round())),
            ),
            _buildSliderTile(
              'Timeout radar/distancia → reloj',
              '${_config.sleepTimeoutS}s',
              _config.sleepTimeoutS.toDouble(),
              3,
              30,
              (value) => _updateConfig((c) => c.copyWith(sleepTimeoutS: value.round())),
            ),
          ]),

          _buildSection('Batería', [
            _buildSliderTile(
              'Umbral batería baja',
              '${_config.lowBatteryThreshold}%',
              _config.lowBatteryThreshold.toDouble(),
              5,
              30,
              (value) => _updateConfig((c) => c.copyWith(lowBatteryThreshold: value.round())),
              activeColor: const Color(0xFFFF6644),
            ),
          ]),

          _buildSection('GPS Dynamic Polling', [
            _buildSliderTile(
              'Precisión (<500m)',
              '${_config.gpsIntervalPrecisionS}s',
              _config.gpsIntervalPrecisionS.toDouble(),
              1,
              10,
              (value) => _updateConfig((c) => c.copyWith(gpsIntervalPrecisionS: value.round())),
              activeColor: const Color(0xFF00CCFF),
            ),
            _buildSliderTile(
              'Cercano (<10km)',
              '${_config.gpsIntervalNearS}s',
              _config.gpsIntervalNearS.toDouble(),
              15,
              120,
              (value) => _updateConfig((c) => c.copyWith(gpsIntervalNearS: value.round())),
              activeColor: const Color(0xFF00CC88),
            ),
            _buildSliderTile(
              'Lejos (10-50km)',
              '${(_config.gpsIntervalFarS / 60).toStringAsFixed(0)} min',
              _config.gpsIntervalFarS.toDouble(),
              60,
              600,
              (value) => _updateConfig((c) => c.copyWith(gpsIntervalFarS: value.round())),
              activeColor: const Color(0xFFFFCC00),
            ),
          ]),

          _buildSection('Brújula y Calibración LIS3MDL', [
            Padding(
              padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
              child: SizedBox(
                width: double.infinity,
                child: ElevatedButton.icon(
                  onPressed: connected
                      ? () {
                          Navigator.push(
                            context,
                            MaterialPageRoute(builder: (_) => const CompassDiagnosticScreen()),
                          );
                        }
                      : null,
                  icon: const Icon(Icons.explore_rounded, size: 18),
                  label: Text(connected ? 'Diagnóstico y Calibración de Brújula (15s)' : 'Conecta el reloj primero'),
                  style: ElevatedButton.styleFrom(
                    backgroundColor: const Color(0xFF2C1E2D),
                    foregroundColor: const Color(0xFFFF88EE),
                    padding: const EdgeInsets.symmetric(vertical: 14),
                    shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(10)),
                  ),
                ),
              ),
            ),
          ]),

          _buildSection('Actualización de Firmware (OTA / DFU)', [
            Padding(
              padding: const EdgeInsets.all(16),
              child: Column(
                children: [
                  SizedBox(
                    width: double.infinity,
                    child: ElevatedButton.icon(
                      onPressed: connected
                          ? () async {
                              final confirm = await showDialog<bool>(
                                context: context,
                                builder: (dialogCtx) => AlertDialog(
                                  backgroundColor: const Color(0xFF141428),
                                  title: const Text('Reiniciar en Modo OTA / DFU', style: TextStyle(color: Colors.white)),
                                  content: const Text(
                                    'El reloj vibrará y sus LEDs se iluminarán en naranja.\n\nSe reiniciará inmediatamente en Modo DFU para recibir firmware por aire mediante nRF Connect.',
                                    style: TextStyle(color: Colors.white70),
                                  ),
                                  actions: [
                                    TextButton(
                                      onPressed: () => Navigator.pop(dialogCtx, false),
                                      child: const Text('Cancelar', style: TextStyle(color: Colors.white54)),
                                    ),
                                    ElevatedButton(
                                      onPressed: () => Navigator.pop(dialogCtx, true),
                                      style: ElevatedButton.styleFrom(backgroundColor: Colors.orangeAccent),
                                      child: const Text('Iniciar DFU'),
                                    ),
                                  ],
                                ),
                              );
                              if (confirm == true) {
                                try {
                                  await bleService.triggerOTA();
                                  if (context.mounted) {
                                    ScaffoldMessenger.of(context).showSnackBar(
                                      const SnackBar(
                                        content: Text('Reloj reiniciado en Modo DFU. Conéctate desde nRF Connect para flashear.'),
                                        backgroundColor: Colors.orange,
                                      ),
                                    );
                                  }
                                } catch (e) {
                                  if (context.mounted) {
                                    ScaffoldMessenger.of(context).showSnackBar(
                                      SnackBar(content: Text('Error al activar OTA: $e'), backgroundColor: Colors.red),
                                    );
                                  }
                                }
                              }
                            }
                          : null,
                      icon: const Icon(Icons.system_update_rounded, size: 20),
                      label: Text(connected ? 'Iniciar Modo OTA / DFU en el Reloj' : 'Conecta el reloj primero'),
                      style: ElevatedButton.styleFrom(
                        backgroundColor: const Color(0xFFE65100),
                        foregroundColor: Colors.white,
                        padding: const EdgeInsets.symmetric(vertical: 14),
                        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(10)),
                      ),
                    ),
                  ),
                  const SizedBox(height: 8),
                  const Text(
                    'Permite flashear el firmware inalámbricamente desde apps como nRF Connect.',
                    textAlign: TextAlign.center,
                    style: TextStyle(color: Colors.white38, fontSize: 12),
                  ),
                ],
              ),
            ),
          ]),
        ],
      ),
    );
  }
}

// ============================================================================
// HELPER TILES
// ============================================================================

Widget _buildSection(String title, List<Widget> children) {
  return Container(
    margin: const EdgeInsets.only(bottom: 20),
    decoration: BoxDecoration(
      color: const Color(0xFF141428),
      borderRadius: BorderRadius.circular(16),
      border: Border.all(color: Colors.white.withValues(alpha: 0.05)),
    ),
    child: Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Padding(
          padding: const EdgeInsets.fromLTRB(16, 16, 16, 8),
          child: Text(
            title.toUpperCase(),
            style: TextStyle(
              fontSize: 11,
              fontWeight: FontWeight.w700,
              letterSpacing: 1.2,
              color: Colors.white.withValues(alpha: 0.35),
            ),
          ),
        ),
        ...children,
      ],
    ),
  );
}

Widget _buildSwitchTile(String title, String subtitle, bool value, ValueChanged<bool> onChanged) {
  return SwitchListTile(
    title: Text(title, style: const TextStyle(fontWeight: FontWeight.w600, fontSize: 14, color: Colors.white)),
    subtitle: Text(subtitle, style: TextStyle(fontSize: 12, color: Colors.white.withValues(alpha: 0.35))),
    value: value,
    onChanged: onChanged,
    activeColor: const Color(0xFF4488FF),
    contentPadding: const EdgeInsets.symmetric(horizontal: 16, vertical: 4),
  );
}

Widget _buildSliderTile(
  String title,
  String valueText,
  double value,
  double min,
  double max,
  ValueChanged<double> onChanged, {
  Color activeColor = const Color(0xFF4488FF),
}) {
  return Padding(
    padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
    child: Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Row(
          mainAxisAlignment: MainAxisAlignment.spaceBetween,
          children: [
            Text(title, style: const TextStyle(fontWeight: FontWeight.w600, fontSize: 14, color: Colors.white)),
            Text(valueText, style: TextStyle(fontWeight: FontWeight.w700, fontSize: 13, color: activeColor)),
          ],
        ),
        SliderTheme(
          data: SliderThemeData(
            activeTrackColor: activeColor,
            thumbColor: activeColor,
            inactiveTrackColor: Colors.white.withValues(alpha: 0.1),
            trackHeight: 3,
          ),
          child: Slider(value: value.clamp(min, max), min: min, max: max, onChanged: onChanged),
        ),
      ],
    ),
  );
}

Widget _buildColorTile(
  BuildContext context,
  String title,
  Color color,
  String hexCode,
  ValueChanged<String> onHexChanged,
) {
  return ListTile(
    contentPadding: const EdgeInsets.symmetric(horizontal: 16, vertical: 2),
    title: Text(title, style: const TextStyle(fontWeight: FontWeight.w600, fontSize: 14, color: Colors.white)),
    subtitle: Text('#$hexCode', style: TextStyle(fontSize: 11, fontFamily: 'monospace', color: Colors.white.withValues(alpha: 0.3))),
    trailing: GestureDetector(
      onTap: () async {
        final resultHex = await showDialog<String>(
          context: context,
          builder: (dialogContext) => _ColorPickerDialog(
            title: title,
            initialColor: color,
          ),
        );
        if (resultHex != null && resultHex.isNotEmpty) {
          onHexChanged(resultHex);
        }
      },
      child: Container(
        width: 32,
        height: 32,
        decoration: BoxDecoration(
          color: color,
          shape: BoxShape.circle,
          border: Border.all(color: Colors.white.withValues(alpha: 0.2), width: 2),
        ),
      ),
    ),
  );
}

class _ColorPickerDialog extends StatefulWidget {
  final String title;
  final Color initialColor;

  const _ColorPickerDialog({
    required this.title,
    required this.initialColor,
  });

  @override
  State<_ColorPickerDialog> createState() => _ColorPickerDialogState();
}

class _ColorPickerDialogState extends State<_ColorPickerDialog> with SingleTickerProviderStateMixin {
  late Color _currentColor;
  late TextEditingController _hexController;
  late TabController _tabController;

  @override
  void initState() {
    super.initState();
    _currentColor = widget.initialColor;
    final initialHex = (_currentColor.value & 0xFFFFFFFF).toRadixString(16).padLeft(8, '0').toUpperCase();
    _hexController = TextEditingController(text: initialHex);
    _tabController = TabController(length: 2, vsync: this);
  }

  @override
  void dispose() {
    _hexController.dispose();
    _tabController.dispose();
    super.dispose();
  }

  void _updateFromColor(Color newColor) {
    setState(() {
      _currentColor = newColor;
      _hexController.text = (newColor.value & 0xFFFFFFFF).toRadixString(16).padLeft(8, '0').toUpperCase();
    });
  }

  void _updateFromHex(String text) {
    var hex = text.replaceAll('#', '').trim();
    if (hex.length == 6) hex = 'FF$hex';
    if (hex.length == 8) {
      final val = int.tryParse(hex, radix: 16);
      if (val != null) {
        setState(() {
          _currentColor = Color(val);
        });
      }
    }
  }

  @override
  Widget build(BuildContext context) {
    final r = (_currentColor.value >> 16) & 0xFF;
    final g = (_currentColor.value >> 8) & 0xFF;
    final b = _currentColor.value & 0xFF;

    return AlertDialog(
      backgroundColor: const Color(0xFF141428),
      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(20)),
      title: Column(
        children: [
          Row(
            children: [
              Container(
                width: 36,
                height: 36,
                decoration: BoxDecoration(
                  color: _currentColor,
                  shape: BoxShape.circle,
                  border: Border.all(color: Colors.white24, width: 2),
                ),
              ),
              const SizedBox(width: 12),
              Expanded(
                child: Text(
                  'Color de ${widget.title}',
                  style: const TextStyle(color: Colors.white, fontSize: 16, fontWeight: FontWeight.w700),
                ),
              ),
            ],
          ),
          const SizedBox(height: 12),
          TabBar(
            controller: _tabController,
            indicatorColor: const Color(0xFF4488FF),
            labelColor: const Color(0xFF4488FF),
            unselectedLabelColor: Colors.white38,
            tabs: const [
              Tab(text: 'Visual'),
              Tab(text: 'RGB / HEX'),
            ],
          ),
        ],
      ),
      content: SizedBox(
        width: 320,
        height: 340,
        child: TabBarView(
          controller: _tabController,
          children: [
            // Tab 1: Visual Picker
            SingleChildScrollView(
              child: ColorPicker(
                pickerColor: _currentColor,
                onColorChanged: _updateFromColor,
                pickerAreaHeightPercent: 0.75,
                enableAlpha: true,
              ),
            ),

            // Tab 2: Manual RGB Sliders + HEX Input
            SingleChildScrollView(
              child: Column(
                children: [
                  const SizedBox(height: 8),
                  // HEX Field
                  TextField(
                    controller: _hexController,
                    style: const TextStyle(color: Colors.white, fontFamily: 'monospace', fontWeight: FontWeight.w700),
                    decoration: InputDecoration(
                      labelText: 'Código HEX (#AARRGGBB)',
                      labelStyle: TextStyle(color: Colors.white.withValues(alpha: 0.5), fontSize: 12),
                      prefixText: '# ',
                      prefixStyle: const TextStyle(color: Color(0xFF4488FF), fontWeight: FontWeight.bold),
                      filled: true,
                      fillColor: Colors.white.withValues(alpha: 0.05),
                      border: OutlineInputBorder(borderRadius: BorderRadius.circular(10)),
                    ),
                    onChanged: _updateFromHex,
                  ),
                  const SizedBox(height: 16),

                  // Red Slider
                  _buildRgbSlider('Rojo (R)', r, Colors.redAccent, (v) {
                    _updateFromColor(Color.fromARGB((_currentColor.value >> 24) & 0xFF, v.round(), g, b));
                  }),
                  // Green Slider
                  _buildRgbSlider('Verde (G)', g, Colors.greenAccent, (v) {
                    _updateFromColor(Color.fromARGB((_currentColor.value >> 24) & 0xFF, r, v.round(), b));
                  }),
                  // Blue Slider
                  _buildRgbSlider('Azul (B)', b, Colors.blueAccent, (v) {
                    _updateFromColor(Color.fromARGB((_currentColor.value >> 24) & 0xFF, r, g, v.round()));
                  }),
                ],
              ),
            ),
          ],
        ),
      ),
      actions: [
        TextButton(
          onPressed: () => Navigator.pop(context),
          child: const Text('Cancelar', style: TextStyle(color: Colors.white54)),
        ),
        ElevatedButton(
          onPressed: () {
            final hex = (_currentColor.value & 0xFFFFFFFF).toRadixString(16).padLeft(8, '0').toUpperCase();
            Navigator.pop(context, hex);
          },
          style: ElevatedButton.styleFrom(
            backgroundColor: const Color(0xFF4488FF),
            foregroundColor: Colors.white,
          ),
          child: const Text('Aceptar', style: TextStyle(fontWeight: FontWeight.w700)),
        ),
      ],
    );
  }

  Widget _buildRgbSlider(String label, int value, Color activeColor, ValueChanged<double> onChanged) {
    return Column(
      children: [
        Row(
          mainAxisAlignment: MainAxisAlignment.spaceBetween,
          children: [
            Text(label, style: const TextStyle(color: Colors.white70, fontSize: 13, fontWeight: FontWeight.w600)),
            Text('$value', style: TextStyle(color: activeColor, fontSize: 13, fontWeight: FontWeight.bold)),
          ],
        ),
        SliderTheme(
          data: SliderThemeData(
            activeTrackColor: activeColor,
            thumbColor: activeColor,
            inactiveTrackColor: Colors.white10,
            trackHeight: 3,
          ),
          child: Slider(
            value: value.toDouble().clamp(0, 255),
            min: 0,
            max: 255,
            onChanged: onChanged,
          ),
        ),
      ],
    );
  }
}

Widget _buildDropdownTile<T>(
  String title,
  String subtitle,
  T value,
  Map<T, String> items,
  ValueChanged<T?> onChanged,
) {
  return Padding(
    padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
    child: Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(title, style: const TextStyle(fontWeight: FontWeight.w600, fontSize: 14, color: Colors.white)),
        const SizedBox(height: 2),
        Text(subtitle, style: TextStyle(fontSize: 12, color: Colors.white.withValues(alpha: 0.35))),
        const SizedBox(height: 8),
        Container(
          padding: const EdgeInsets.symmetric(horizontal: 12),
          decoration: BoxDecoration(
            color: Colors.white.withValues(alpha: 0.05),
            borderRadius: BorderRadius.circular(10),
            border: Border.all(color: Colors.white.withValues(alpha: 0.1)),
          ),
          child: DropdownButtonHideUnderline(
            child: DropdownButton<T>(
              value: value,
              isExpanded: true,
              dropdownColor: const Color(0xFF141428),
              style: const TextStyle(color: Colors.white, fontSize: 13),
              items: items.entries.map((entry) {
                return DropdownMenuItem<T>(value: entry.key, child: Text(entry.value));
              }).toList(),
              onChanged: onChanged,
            ),
          ),
        ),
      ],
    ),
  );
}

Color _parseColor(String hexColor) {
  return WatchConfig.parseColor(hexColor);
}
