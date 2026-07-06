import 'dart:async';
import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import 'package:shared_preferences/shared_preferences.dart';
import 'package:flutter_foreground_task/flutter_foreground_task.dart';

import '../main.dart';
import '../models/config_model.dart';
import '../repositories/partner_repository.dart';
import '../services/ble_service.dart';
import '../widgets/watch_preview_widget.dart';
import 'wake_calibration_screen.dart';
import 'wrist_flick_calibration_screen.dart';

/// Pantalla de configuración del reloj.
///
/// Permite ajustar:
/// - Colores de las agujas (connected/disconnected)
/// - Brillo global
/// - Timers (clock timeout, sleep timeout)
/// - Umbral de batería baja
/// - Intervalos de GPS polling
/// - Umbral de wake-on-motion
class SettingsScreen extends StatefulWidget {
  const SettingsScreen({super.key});

  @override
  State<SettingsScreen> createState() => _SettingsScreenState();
}

class _SettingsScreenState extends State<SettingsScreen> {
  late WatchConfig _config;
  bool _hasChanges = false;

  @override
  void initState() {
    super.initState();
    final repo = context.read<PartnerRepository>();
    _config = repo.config ?? WatchConfig.defaultFor(repo.myUserId);
  }

  void _updateConfig(WatchConfig Function(WatchConfig) updater) {
    setState(() {
      _config = updater(_config);
      _hasChanges = true;
    });
  }

  Future<void> _saveConfig() async {
    final repo = context.read<PartnerRepository>();
    final bleService = context.read<BleService>();

    try {
      // Guardar en Supabase
      await repo.saveConfig(_config);

      // Enviar al reloj por BLE
      await bleService.writeConfig(_config);

      if (!mounted) return;
      setState(() => _hasChanges = false);
      
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
          content: Text('Configuración guardada'),
          backgroundColor: Color(0xFF1A2A3A),
          behavior: SnackBarBehavior.floating,
        ),
      );
    } catch (e) {
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text('Error al guardar: $e'),
          backgroundColor: Colors.red,
          behavior: SnackBarBehavior.floating,
        ),
      );
    }
  }

  void _showCompassCalibDialog(BuildContext context, BleService bleService) {
    showDialog(
      context: context,
      barrierDismissible: false,
      builder: (BuildContext dialogContext) {
        return _CompassCalibDialog(bleService: bleService);
      },
    );
  }

  @override
  Widget build(BuildContext context) {
    final repo = context.watch<PartnerRepository>();
    final bleService = context.watch<BleService>();
    final connected = bleService.connectionState == BleConnectionState.connected;
    return Scaffold(
      backgroundColor: const Color(0xFF0A0A1A),
      appBar: AppBar(
        backgroundColor: Colors.transparent,
        elevation: 0,
        title: const Text(
          'Configuración',
          style: TextStyle(
            fontWeight: FontWeight.w700,
            letterSpacing: -0.5,
          ),
        ),
        actions: [
          if (_hasChanges)
            TextButton(
              onPressed: _saveConfig,
              child: const Text(
                'Guardar',
                style: TextStyle(
                  color: Color(0xFF4488FF),
                  fontWeight: FontWeight.w600,
                ),
              ),
            ),
        ],
      ),
      body: ListView(
        padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 8),
        children: [
          // ── Preview del reloj ────────────────────────────────────────
          Center(
            child: WatchPreviewWidget(
              mode: WatchPreviewMode.clock,
              config: _config,
              isConnected: true,
              size: 160,
            ),
          ),
          const SizedBox(height: 24),

          // ── Brillo ──────────────────────────────────────────────────
          _buildSection('Brillo', [
            _buildSliderTile(
              'Brillo global',
              '${_config.brightnessPercent}%',
              _config.brightnessPercent.toDouble(),
              10,
              100,
              (value) => _updateConfig(
                  (c) => c.copyWith(brightnessPercent: value.round())),
              activeColor: const Color(0xFFFFCC00),
            ),
            _buildSwitchTile(
              'Brillo logarítmico',
              'Curva gamma para percepción lineal',
              _config.logarithmicBrightness,
              (value) => _updateConfig(
                  (c) => c.copyWith(logarithmicBrightness: value)),
            ),
          ]),

          // ── Timers ──────────────────────────────────────────────────
          _buildSection('Temporizadores', [
            _buildSliderTile(
              'Timeout reloj → sleep',
              '${_config.sleepTimeoutS}s',
              _config.sleepTimeoutS.toDouble(),
              3,
              30,
              (value) => _updateConfig(
                  (c) => c.copyWith(sleepTimeoutS: value.round())),
            ),
            _buildSliderTile(
              'Timeout radar/distancia → reloj',
              '${_config.clockTimeoutS}s',
              _config.clockTimeoutS.toDouble(),
              3,
              30,
              (value) => _updateConfig(
                  (c) => c.copyWith(clockTimeoutS: value.round())),
            ),
          ]),

          // ── Batería ─────────────────────────────────────────────────
          _buildSection('Batería', [
            _buildSliderTile(
              'Umbral batería baja',
              '${_config.lowBatteryThreshold}%',
              _config.lowBatteryThreshold.toDouble(),
              5,
              30,
              (value) => _updateConfig(
                  (c) => c.copyWith(lowBatteryThreshold: value.round())),
              activeColor: const Color(0xFFFF6644),
            ),
          ]),

          // ── Colores (Conectado) ─────────────────────────────────────
          _buildSection('Colores — Conectado', [
            _buildColorTile(
              'Horas',
              _config.hoursConnectedColor,
              _config.colorHoursConnected,
              (hex) => _updateConfig((c) => c.copyWith(colorHoursConnected: hex)),
            ),
            _buildColorTile(
              'Minutos',
              _config.minutesConnectedColor,
              _config.colorMinutesConnected,
              (hex) => _updateConfig((c) => c.copyWith(colorMinutesConnected: hex)),
            ),
            _buildColorTile(
              'Segundos',
              _config.secondsConnectedColor,
              _config.colorSecondsConnected,
              (hex) => _updateConfig((c) => c.copyWith(colorSecondsConnected: hex)),
            ),
          ]),

          // ── Colores (Desconectado) ──────────────────────────────────
          _buildSection('Colores — Desconectado', [
            _buildColorTile(
              'Horas',
              _config.hoursDiscColor,
              _config.colorHoursDisc,
              (hex) => _updateConfig((c) => c.copyWith(colorHoursDisc: hex)),
            ),
            _buildColorTile(
              'Minutos',
              _config.minutesDiscColor,
              _config.colorMinutesDisc,
              (hex) => _updateConfig((c) => c.copyWith(colorMinutesDisc: hex)),
            ),
            _buildColorTile(
              'Segundos',
              _config.secondsDiscColor,
              _config.colorSecondsDisc,
              (hex) => _updateConfig((c) => c.copyWith(colorSecondsDisc: hex)),
            ),
          ]),

          // ── GPS Polling ─────────────────────────────────────────────
          _buildSection('GPS Dynamic Polling', [
            _buildSliderTile(
              'Precisión (<500m)',
              '${_config.gpsIntervalPrecisionS}s',
              _config.gpsIntervalPrecisionS.toDouble(),
              1,
              10,
              (value) => _updateConfig(
                  (c) => c.copyWith(gpsIntervalPrecisionS: value.round())),
              activeColor: const Color(0xFF00CCFF),
            ),
            _buildSliderTile(
              'Cercano (<10km)',
              '${_config.gpsIntervalNearS}s',
              _config.gpsIntervalNearS.toDouble(),
              15,
              120,
              (value) => _updateConfig(
                  (c) => c.copyWith(gpsIntervalNearS: value.round())),
              activeColor: const Color(0xFF00CC88),
            ),
            _buildSliderTile(
              'Lejos (10-50km)',
              '${(_config.gpsIntervalFarS / 60).toStringAsFixed(0)} min',
              _config.gpsIntervalFarS.toDouble(),
              60,
              600,
              (value) => _updateConfig(
                  (c) => c.copyWith(gpsIntervalFarS: value.round())),
              activeColor: const Color(0xFFFFCC00),
            ),
            _buildSliderTile(
              'Remoto (>50km)',
              '${(_config.gpsIntervalRemoteMinS / 60).toStringAsFixed(0)}-${(_config.gpsIntervalRemoteMaxS / 60).toStringAsFixed(0)} min',
              _config.gpsIntervalRemoteMinS.toDouble(),
              120,
              1200,
              (value) => _updateConfig(
                  (c) => c.copyWith(gpsIntervalRemoteMinS: value.round())),
              activeColor: const Color(0xFFFF6600),
            ),
          ]),

          // ── Wake-on-Motion ──────────────────────────────────────────
          _buildSection('Rise-to-Wake', [
            _buildSliderTile(
              'Sensibilidad de wake',
              '${(_config.wakeThreshold * 62.5).toInt()} mg',
              _config.wakeThreshold.toDouble(),
              1,
              63,
              (value) => _updateConfig(
                  (c) => c.copyWith(wakeThreshold: value.round())),
              activeColor: const Color(0xFF8866FF),
            ),
            Padding(
              padding:
                  const EdgeInsets.symmetric(horizontal: 16, vertical: 4),
              child: Text(
                '1 LSB = 62.5 mg. Menor valor = más sensible.\n'
                'Recomendado: 500 mg (reg 0x08). Si despierta solo: subir. Si no detecta: bajar.',
                style: TextStyle(
                  fontSize: 11,
                  color: Colors.white.withValues(alpha: 0.25),
                  height: 1.4,
                ),
              ),
            ),
            Padding(
              padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
              child: SizedBox(
                width: double.infinity,
                child: ElevatedButton.icon(
                  onPressed: () async {
                    await Navigator.push(
                      context,
                      MaterialPageRoute(
                        builder: (_) => const WakeCalibrationScreen(),
                      ),
                    );
                    if (context.mounted) setState(() => _config = context.read<PartnerRepository>().config ?? _config);
                  },
                  icon: const Icon(Icons.tune_rounded, size: 18),
                  label: const Text('Calibración avanzada'),
                  style: ElevatedButton.styleFrom(
                    backgroundColor: const Color(0xFF1E1C2E),
                    foregroundColor: const Color(0xFFBB88FF),
                    padding: const EdgeInsets.symmetric(vertical: 14),
                    shape: RoundedRectangleBorder(
                      borderRadius: BorderRadius.circular(10),
                    ),
                  ),
                ),
              ),
            ),
          ]),

          // ── Giro de muñeca ──────────────────────────────────────────
          _buildSection('Giro de muñeca (Wrist Flick)', [
            _buildSliderTile(
              'Sensibilidad de giro',
              '${_config.gyroThreshold} dps',
              _config.gyroThreshold.toDouble(),
              100,
              2000,
              (value) => _updateConfig(
                  (c) => c.copyWith(gyroThreshold: value.round())),
              activeColor: const Color(0xFF00CC88),
            ),
            _buildSliderTile(
              'Ventana de doble giro',
              '${_config.doubleFlickWindowMs} ms',
              _config.doubleFlickWindowMs.toDouble(),
              400,
              1200,
              (value) => _updateConfig(
                  (c) => c.copyWith(doubleFlickWindowMs: value.round())),
              activeColor: const Color(0xFF00FFCC),
            ),
            Padding(
              padding:
                  const EdgeInsets.symmetric(horizontal: 16, vertical: 4),
              child: Text(
                'Sensibilidad: Umbral en dps (menor = más sensible). Recomendado: 260 dps.\n'
                'Ventana de doble giro: Tiempo límite entre los dos giros rápidos para que cuente como doble flick. Recomendado: 800 ms.',
                style: TextStyle(
                  fontSize: 11,
                  color: Colors.white.withValues(alpha: 0.25),
                  height: 1.4,
                ),
              ),
            ),
            Padding(
              padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
              child: SizedBox(
                width: double.infinity,
                child: ElevatedButton.icon(
                  onPressed: () => Navigator.push(
                    context,
                    MaterialPageRoute(
                      builder: (_) => const WristFlickCalibrationScreen(),
                    ),
                  ),
                  icon: const Icon(Icons.sync_rounded, size: 18),
                  label: const Text('Calibrar giro'),
                  style: ElevatedButton.styleFrom(
                    backgroundColor: const Color(0xFF1C2E24),
                    foregroundColor: const Color(0xFF88FFBB),
                    padding: const EdgeInsets.symmetric(vertical: 14),
                    shape: RoundedRectangleBorder(
                      borderRadius: BorderRadius.circular(10),
                    ),
                  ),
                ),
              ),
            ),
          ]),

          // ── Brújula ──────────────────────────────────────────────────
          _buildSection('Brújula (Magnetómetro)', [
            Padding(
              padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 4),
              child: Text(
                'Calibra el magnetómetro interno del reloj para corregir desviaciones causadas por interferencias magnéticas.',
                style: TextStyle(
                  fontSize: 11,
                  color: Colors.white.withValues(alpha: 0.25),
                  height: 1.4,
                ),
              ),
            ),
            Padding(
              padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
              child: SizedBox(
                width: double.infinity,
                child: ElevatedButton.icon(
                  onPressed: connected ? () => _showCompassCalibDialog(context, bleService) : null,
                  icon: const Icon(Icons.explore_rounded, size: 18),
                  label: Text(connected
                      ? 'Calibrar brújula'
                      : 'Conecta el reloj primero'),
                  style: ElevatedButton.styleFrom(
                    backgroundColor: const Color(0xFF2C1E2D),
                    foregroundColor: const Color(0xFFFF88EE),
                    disabledBackgroundColor: Colors.white.withValues(alpha: 0.05),
                    disabledForegroundColor: Colors.white.withValues(alpha: 0.2),
                    padding: const EdgeInsets.symmetric(vertical: 14),
                    shape: RoundedRectangleBorder(
                      borderRadius: BorderRadius.circular(10),
                    ),
                  ),
                ),
              ),
            ),
          ]),

          // ── Notificaciones ──────────────────────────────────────────
          _buildSection('Notificaciones', [
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
                  onPressed: connected
                      ? () async {
                          try {
                            await bleService.sendHapticCommand();
                            if (!mounted) return;
                            ScaffoldMessenger.of(context).showSnackBar(
                              SnackBar(
                                content: const Text('Toque de prueba enviado al reloj'),
                                backgroundColor: const Color(0xFF1E2D2A),
                                behavior: SnackBarBehavior.floating,
                                shape: RoundedRectangleBorder(
                                  borderRadius: BorderRadius.circular(8),
                                ),
                              ),
                            );
                          } catch (e) {
                            if (!mounted) return;
                            ScaffoldMessenger.of(context).showSnackBar(
                              SnackBar(
                                content: Text('Error al enviar toque: $e'),
                                backgroundColor: const Color(0xFF3E1C1C),
                                behavior: SnackBarBehavior.floating,
                                shape: RoundedRectangleBorder(
                                  borderRadius: BorderRadius.circular(8),
                                ),
                              ),
                            );
                          }
                        }
                      : null,
                  icon: const Icon(Icons.vibration_rounded, size: 18),
                  label: Text(connected
                      ? 'Probar vibración (Enviar toque)'
                      : 'Conecta el reloj primero'),
                  style: ElevatedButton.styleFrom(
                    backgroundColor: const Color(0xFF1C2E2A),
                    foregroundColor: const Color(0xFF88FFBB),
                    disabledBackgroundColor: Colors.white.withValues(alpha: 0.05),
                    disabledForegroundColor: Colors.white.withValues(alpha: 0.2),
                    padding: const EdgeInsets.symmetric(vertical: 14),
                    shape: RoundedRectangleBorder(
                      borderRadius: BorderRadius.circular(10),
                    ),
                  ),
                ),
              ),
            ),
          ]),

          // ── Firmware Update (OTA) ───────────────────────────────────

          _buildSection('Firmware', [
            Padding(
              padding: const EdgeInsets.all(16),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    'Actualizar firmware por BLE (OTA). El reloj entrará en modo '
                    'DFU y se podrá flashear sin abrirlo.',
                    style: TextStyle(
                      fontSize: 12,
                      color: Colors.white.withValues(alpha: 0.35),
                      height: 1.4,
                    ),
                  ),
                  const SizedBox(height: 12),
                  SizedBox(
                    width: double.infinity,
                    child: Consumer<BleService>(
                      builder: (context, ble, _) {
                        final connected = ble.connectionState ==
                            BleConnectionState.connected;
                        return ElevatedButton.icon(
                          onPressed: connected ? () => _triggerOTA(ble) : null,
                          icon: const Icon(Icons.system_update, size: 18),
                          label: Text(connected
                              ? 'Entrar en modo DFU'
                              : 'Conecta el reloj primero'),
                          style: ElevatedButton.styleFrom(
                            backgroundColor: const Color(0xFF2A1A3A),
                            foregroundColor: const Color(0xFFBB88FF),
                            disabledBackgroundColor:
                                Colors.white.withValues(alpha: 0.05),
                            disabledForegroundColor:
                                Colors.white.withValues(alpha: 0.2),
                            padding: const EdgeInsets.symmetric(vertical: 14),
                            shape: RoundedRectangleBorder(
                              borderRadius: BorderRadius.circular(10),
                            ),
                          ),
                        );
                      },
                    ),
                  ),
                ],
              ),
            ),
          ]),

          // ── Rol de Usuario ─────────────────────────────────────────
          _buildSection('Sesión', [
            Padding(
              padding: const EdgeInsets.all(16),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    'Rol actual del usuario: ${repo.myUserId == "A" ? "Usuario A (Rol A)" : "Usuario B (Rol B)"}\n'
                    'Al cambiar el rol, se reiniciará la sincronización con el servidor.',
                    style: TextStyle(
                      fontSize: 12,
                      color: Colors.white.withValues(alpha: 0.35),
                      height: 1.4,
                    ),
                  ),
                  const SizedBox(height: 12),
                  SizedBox(
                    width: double.infinity,
                    child: ElevatedButton.icon(
                      onPressed: () => _changeUserRole(repo),
                      icon: const Icon(Icons.swap_horiz, size: 18),
                      label: const Text('Cambiar Rol de Usuario'),
                      style: ElevatedButton.styleFrom(
                        backgroundColor: const Color(0xFF1F1A2A),
                        foregroundColor: const Color(0xFFFF9988),
                        disabledBackgroundColor:
                            Colors.white.withValues(alpha: 0.05),
                        disabledForegroundColor:
                            Colors.white.withValues(alpha: 0.2),
                        padding: const EdgeInsets.symmetric(vertical: 14),
                        shape: RoundedRectangleBorder(
                          borderRadius: BorderRadius.circular(10),
                        ),
                      ),
                    ),
                  ),
                ],
              ),
            ),
          ]),

          const SizedBox(height: 40),
        ],
      ),
    );
  }

  // ── Diálogos y Helpers ───────────────────────────────────────────────────

  void _showColorPicker(String title, String currentHexAarrggbb, ValueChanged<String> onSelected) {
    String currentRgb = currentHexAarrggbb.length == 8 
        ? currentHexAarrggbb.substring(2) 
        : currentHexAarrggbb;
        
    final controller = TextEditingController(text: currentRgb);
    Color previewColor = WatchConfig.parseColor('FF$currentRgb');
    bool isValid = true;

    final List<Map<String, dynamic>> presets = [
      {'name': 'Ámbar', 'hex': 'FFB900'},
      {'name': 'Cian', 'hex': '00CCFF'},
      {'name': 'Menta', 'hex': '00CC88'},
      {'name': 'Rosa', 'hex': 'FF6699'},
      {'name': 'Azul', 'hex': '4488FF'},
      {'name': 'Púrpura', 'hex': 'BB88FF'},
      {'name': 'Blanco Cálido', 'hex': 'FFDCB4'},
      {'name': 'Rojo Neon', 'hex': 'FF4444'},
    ];

    showDialog(
      context: context,
      builder: (ctx) {
        return StatefulBuilder(
          builder: (context, setStateDialog) {
            void updatePreview(String text) {
              final cleaned = text.replaceAll('#', '').trim();
              final regExp = RegExp(r'^[0-9a-fA-F]{6}$');
              if (regExp.hasMatch(cleaned)) {
                setStateDialog(() {
                  previewColor = WatchConfig.parseColor('FF$cleaned');
                  isValid = true;
                });
              } else {
                setStateDialog(() {
                  isValid = false;
                });
              }
            }

            return AlertDialog(
              backgroundColor: const Color(0xFF1A1A2E),
              shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(16)),
              title: Text('Elegir Color para $title', style: const TextStyle(color: Colors.white)),
              content: SingleChildScrollView(
                child: Column(
                  mainAxisSize: MainAxisSize.min,
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: [
                    Center(
                      child: Container(
                        width: 64,
                        height: 64,
                        decoration: BoxDecoration(
                          color: isValid ? previewColor : Colors.grey.shade800,
                          shape: BoxShape.circle,
                          boxShadow: isValid
                              ? [
                                  BoxShadow(
                                    color: previewColor.withValues(alpha: 0.4),
                                    blurRadius: 16,
                                    spreadRadius: 2,
                                  )
                                ]
                              : null,
                          border: Border.all(
                            color: Colors.white24,
                            width: 2,
                          ),
                        ),
                        child: !isValid
                            ? const Icon(Icons.help_outline, color: Colors.white54, size: 32)
                            : null,
                      ),
                    ),
                    const SizedBox(height: 24),
                    TextField(
                      controller: controller,
                      onChanged: updatePreview,
                      style: const TextStyle(fontFamily: 'monospace', color: Colors.white),
                      decoration: InputDecoration(
                        labelText: 'Código Hexadecimal (RRGGBB)',
                        labelStyle: const TextStyle(color: Colors.white60),
                        prefixText: '# ',
                        prefixStyle: const TextStyle(color: Colors.white54, fontSize: 16),
                        errorText: isValid ? null : 'Código inválido (ej: FF5500)',
                        enabledBorder: const OutlineInputBorder(
                          borderSide: BorderSide(color: Colors.white24),
                        ),
                        focusedBorder: const OutlineInputBorder(
                          borderSide: BorderSide(color: Color(0xFF4488FF)),
                        ),
                        errorBorder: const OutlineInputBorder(
                          borderSide: BorderSide(color: Colors.redAccent),
                        ),
                        focusedErrorBorder: const OutlineInputBorder(
                          borderSide: BorderSide(color: Colors.redAccent),
                        ),
                      ),
                    ),
                    const SizedBox(height: 20),
                    const Text(
                      'PRESETS RECOMENDADOS',
                      style: TextStyle(
                        fontSize: 10,
                        fontWeight: FontWeight.bold,
                        color: Colors.white38,
                        letterSpacing: 1.0,
                      ),
                    ),
                    const SizedBox(height: 10),
                    Wrap(
                      spacing: 8,
                      runSpacing: 8,
                      children: presets.map((p) {
                        final pHex = p['hex'] as String;
                        final pColor = WatchConfig.parseColor('FF$pHex');
                        return InkWell(
                          onTap: () {
                            controller.text = pHex;
                            updatePreview(pHex);
                          },
                          borderRadius: BorderRadius.circular(8),
                          child: Container(
                            padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
                            decoration: BoxDecoration(
                              color: Colors.white.withValues(alpha: 0.04),
                              border: Border.all(
                                color: controller.text.toUpperCase() == pHex
                                    ? pColor
                                    : Colors.white.withValues(alpha: 0.05),
                                width: 1.5,
                              ),
                              borderRadius: BorderRadius.circular(8),
                            ),
                            child: Row(
                              mainAxisSize: MainAxisSize.min,
                              children: [
                                Container(
                                  width: 12,
                                  height: 12,
                                  decoration: BoxDecoration(
                                    color: pColor,
                                    shape: BoxShape.circle,
                                  ),
                                ),
                                const SizedBox(width: 6),
                                Text(
                                  p['name'] as String,
                                  style: const TextStyle(fontSize: 11, color: Colors.white70),
                                ),
                              ],
                            ),
                          ),
                        );
                      }).toList(),
                    ),
                  ],
                ),
              ),
              actions: [
                TextButton(
                  onPressed: () => Navigator.pop(ctx),
                  child: const Text('Cancelar', style: TextStyle(color: Colors.grey)),
                ),
                TextButton(
                  onPressed: isValid
                      ? () {
                          final cleaned = controller.text.replaceAll('#', '').trim().toUpperCase();
                          onSelected('FF$cleaned');
                          Navigator.pop(ctx);
                        }
                      : null,
                  child: const Text('Seleccionar', style: TextStyle(color: Color(0xFF4488FF))),
                ),
              ],
            );
          },
        );
      },
    );
  }

  Future<void> _startCalibration(BleService ble) async {
    int progress = 0;
    bool completed = false;

    showDialog(
      context: context,
      barrierDismissible: false,
      builder: (ctx) {
        return StatefulBuilder(
          builder: (context, setStateDialog) {
            ble.onCalibrationProgress = (samplesDone) {
              setStateDialog(() {
                progress = samplesDone;
                if (progress >= 5) {
                  completed = true;
                }
              });
              if (progress >= 5) {
                Future.delayed(const Duration(milliseconds: 1500), () {
                  if (ctx.mounted) {
                    Navigator.pop(ctx);
                  }
                });
              }
            };

            return AlertDialog(
              backgroundColor: const Color(0xFF121224),
              shape: RoundedRectangleBorder(
                  borderRadius: BorderRadius.circular(16)),
              title: Row(
                children: [
                  const Icon(Icons.fitness_center_rounded, color: Color(0xFFBB88FF)),
                  const SizedBox(width: 8),
                  Text(
                    completed ? '¡Calibración Completada!' : 'Calibrando...',
                    style: const TextStyle(color: Colors.white, fontSize: 18),
                  ),
                ],
              ),
              content: Column(
                mainAxisSize: MainAxisSize.min,
                children: [
                  const SizedBox(height: 12),
                  if (!completed) ...[
                    const Text(
                      'Realiza el gesto de levantar la muñeca para mirar la hora. Hazlo con normalidad 5 veces consecutivas.',
                      style: TextStyle(color: Colors.white70, fontSize: 13, height: 1.4),
                    ),
                    const SizedBox(height: 24),
                    Text(
                      'Gesto $progress de 5 registrado',
                      style: const TextStyle(
                        color: Color(0xFFBB88FF),
                        fontSize: 16,
                        fontWeight: FontWeight.bold,
                      ),
                    ),
                    const SizedBox(height: 12),
                    LinearProgressIndicator(
                      value: progress / 5.0,
                      backgroundColor: Colors.white.withValues(alpha: 0.05),
                      color: const Color(0xFFBB88FF),
                      minHeight: 6,
                      borderRadius: BorderRadius.circular(3),
                    ),
                  ] else ...[
                    const Icon(Icons.check_circle_rounded,
                        color: Color(0xFF00CC88), size: 64),
                    const SizedBox(height: 16),
                    const Text(
                      'Se ha calculado tu umbral óptimo y guardado en el reloj.',
                      style: TextStyle(color: Colors.white70, fontSize: 13),
                      textAlign: TextAlign.center,
                    ),
                  ],
                  const SizedBox(height: 12),
                ],
              ),
              actions: [
                if (!completed)
                  TextButton(
                    onPressed: () {
                      ble.cancelCalibration();
                      Navigator.pop(ctx);
                    },
                    child: const Text('Cancelar',
                        style: TextStyle(color: Colors.grey)),
                  ),
              ],
            );
          },
        );
      },
    );

    await ble.startCalibration();
  }

  Future<void> _changeUserRole(PartnerRepository repo) async {
    final confirm = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        backgroundColor: const Color(0xFF1A1A2E),
        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(16)),
        title: const Text('¿Cambiar Rol de Usuario?',
            style: TextStyle(color: Colors.white)),
        content: Text(
          'Esto cambiará tu rol actual (${repo.myUserId}) al rol contrario (${repo.partnerUserId}).\n\n'
          'La app se reiniciará para aplicar los cambios y sincronizarse con Supabase.',
          style: const TextStyle(color: Colors.white70, fontSize: 14),
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(ctx, false),
            child:
                const Text('Cancelar', style: TextStyle(color: Colors.grey)),
          ),
          TextButton(
            onPressed: () => Navigator.pop(ctx, true),
            child: const Text('Cambiar y Reiniciar',
                style: TextStyle(color: Color(0xFFFF9988))),
          ),
        ],
      ),
    );

    if (confirm == true) {
      final prefs = await SharedPreferences.getInstance();
      await prefs.setString('user_role', repo.partnerUserId);

      if (mounted) {
        Navigator.of(context).pushAndRemoveUntil(
          MaterialPageRoute(
            builder: (_) => const WithForegroundTask(child: AppBootstrapper()),
          ),
          (route) => false,
        );
      }
    }
  }

  // ── OTA ──────────────────────────────────────────────────────────────────

  Future<void> _triggerOTA(BleService ble) async {
    final confirm = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        backgroundColor: const Color(0xFF1A1A2E),
        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(16)),
        title: const Text('¿Actualizar firmware?',
            style: TextStyle(color: Colors.white)),
        content: const Text(
          'El reloj se reiniciará en modo DFU (bootloader). '
          'Después podrás flashear el nuevo firmware con una app DFU '
          '(ej. nRF Connect).\n\n'
          '⚠️ El reloj se desconectará.',
          style: TextStyle(color: Colors.white70, fontSize: 14),
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(ctx, false),
            child:
                const Text('Cancelar', style: TextStyle(color: Colors.grey)),
          ),
          TextButton(
            onPressed: () => Navigator.pop(ctx, true),
            child: const Text('Actualizar',
                style: TextStyle(color: Color(0xFFBB88FF))),
          ),
        ],
      ),
    );

    if (confirm == true) {
      await ble.triggerOTA();
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: const Text(
                '🔄 Reloj reiniciando en modo DFU. Usa nRF Connect para flashear.'),
            backgroundColor: const Color(0xFF2A1A3A),
            duration: const Duration(seconds: 8),
            behavior: SnackBarBehavior.floating,
            shape:
                RoundedRectangleBorder(borderRadius: BorderRadius.circular(8)),
          ),
        );
      }
    }
  }

  // ── Widgets de sección ─────────────────────────────────────────────────

  Widget _buildSection(String title, List<Widget> children) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Padding(
          padding: const EdgeInsets.fromLTRB(0, 20, 0, 8),
          child: Text(
            title.toUpperCase(),
            style: TextStyle(
              fontSize: 11,
              fontWeight: FontWeight.w700,
              letterSpacing: 1.2,
              color: Colors.white.withValues(alpha: 0.3),
            ),
          ),
        ),
        Container(
          decoration: BoxDecoration(
            color: Colors.white.withValues(alpha: 0.04),
            borderRadius: BorderRadius.circular(16),
            border: Border.all(color: Colors.white.withValues(alpha: 0.04)),
          ),
          child: Column(children: children),
        ),
      ],
    );
  }

  Widget _buildSliderTile(
    String label,
    String valueText,
    double value,
    double min,
    double max,
    ValueChanged<double> onChanged, {
    Color activeColor = const Color(0xFF4488FF),
  }) {
    return Padding(
      padding: const EdgeInsets.fromLTRB(16, 12, 16, 4),
      child: Column(
        children: [
          Row(
            mainAxisAlignment: MainAxisAlignment.spaceBetween,
            children: [
              Text(
                label,
                style: TextStyle(
                  fontSize: 14,
                  color: Colors.white.withValues(alpha: 0.75),
                ),
              ),
              Text(
                valueText,
                style: TextStyle(
                  fontSize: 14,
                  fontWeight: FontWeight.w600,
                  color: activeColor,
                  fontFeatures: const [FontFeature.tabularFigures()],
                ),
              ),
            ],
          ),
          SliderTheme(
            data: SliderThemeData(
              activeTrackColor: activeColor.withValues(alpha: 0.6),
              inactiveTrackColor: Colors.white.withValues(alpha: 0.06),
              thumbColor: activeColor,
              overlayColor: activeColor.withValues(alpha: 0.1),
              trackHeight: 3,
              thumbShape:
                  const RoundSliderThumbShape(enabledThumbRadius: 6),
            ),
            child: Slider(
              value: value.clamp(min, max),
              min: min,
              max: max,
              onChanged: onChanged,
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildSwitchTile(
    String label,
    String subtitle,
    bool value,
    ValueChanged<bool> onChanged,
  ) {
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
      child: Row(
        children: [
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  label,
                  style: TextStyle(
                    fontSize: 14,
                    color: Colors.white.withValues(alpha: 0.75),
                  ),
                ),
                Text(
                  subtitle,
                  style: TextStyle(
                    fontSize: 11,
                    color: Colors.white.withValues(alpha: 0.3),
                  ),
                ),
              ],
            ),
          ),
          Switch.adaptive(
            value: value,
            onChanged: onChanged,
            activeColor: const Color(0xFF4488FF),
          ),
        ],
      ),
    );
  }

  Widget _buildColorTile(String label, Color color, String hexValue, ValueChanged<String> onColorSelected) {
    return Material(
      color: Colors.transparent,
      child: InkWell(
        onTap: () => _showColorPicker(label, hexValue, onColorSelected),
        child: Padding(
          padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
          child: Row(
            children: [
              // Preview de color
              Container(
                width: 28,
                height: 28,
                decoration: BoxDecoration(
                  color: color,
                  shape: BoxShape.circle,
                  boxShadow: [
                    BoxShadow(
                      color: color.withValues(alpha: 0.3),
                      blurRadius: 8,
                    ),
                  ],
                ),
              ),
              const SizedBox(width: 12),
              Expanded(
                child: Text(
                  label,
                  style: TextStyle(
                    fontSize: 14,
                    color: Colors.white.withValues(alpha: 0.75),
                  ),
                ),
              ),
              Text(
                '#${hexValue.length == 8 ? hexValue.substring(2) : hexValue}',
                style: TextStyle(
                  fontSize: 12,
                  fontFamily: 'monospace',
                  color: Colors.white.withValues(alpha: 0.3),
                ),
              ),
              const SizedBox(width: 4),
              Icon(
                Icons.chevron_right_rounded,
                size: 16,
                color: Colors.white.withValues(alpha: 0.2),
              ),
            ],
          ),
        ),
      ),
    );
  }

  Widget _buildDropdownTile<T>(
    String label,
    String subtitle,
    T currentValue,
    Map<T, String> options,
    ValueChanged<T?> onChanged,
  ) {
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
      child: Row(
        children: [
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  label,
                  style: TextStyle(
                    fontSize: 14,
                    color: Colors.white.withValues(alpha: 0.75),
                  ),
                ),
                const SizedBox(height: 2),
                Text(
                  subtitle,
                  style: TextStyle(
                    fontSize: 11,
                    color: Colors.white.withValues(alpha: 0.35),
                  ),
                ),
              ],
            ),
          ),
          const SizedBox(width: 8),
          Theme(
            data: Theme.of(context).copyWith(
              canvasColor: const Color(0xFF1A1A2E),
            ),
            child: DropdownButton<T>(
              value: currentValue,
              items: options.entries.map((entry) {
                return DropdownMenuItem<T>(
                  value: entry.key,
                  child: Text(
                    entry.value,
                    style: const TextStyle(color: Colors.white, fontSize: 13),
                  ),
                );
              }).toList(),
              onChanged: onChanged,
              underline: const SizedBox(),
              icon: const Icon(Icons.arrow_drop_down_rounded, color: Colors.white54),
              dropdownColor: const Color(0xFF16162A),
              style: const TextStyle(color: Colors.white, fontSize: 13),
            ),
          ),
        ],
      ),
    );
  }
}

class _CompassCalibDialog extends StatefulWidget {
  final BleService bleService;
  const _CompassCalibDialog({required this.bleService});

  @override
  State<_CompassCalibDialog> createState() => _CompassCalibDialogState();
}

class _CompassCalibDialogState extends State<_CompassCalibDialog>
    with SingleTickerProviderStateMixin {
  int _secondsLeft = 10;
  Timer? _timer;
  bool _isDone = false;
  late AnimationController _rotationCtrl;

  @override
  void initState() {
    super.initState();
    _rotationCtrl = AnimationController(
      vsync: this,
      duration: const Duration(seconds: 2),
    )..repeat();

    // Iniciar calibración
    widget.bleService.startCompassCalibration().catchError((e) {
      print('[COMPASS] Error starting calibration: $e');
    });

    _timer = Timer.periodic(const Duration(seconds: 1), (timer) {
      if (_secondsLeft > 1) {
        setState(() {
          _secondsLeft--;
        });
      } else {
        _timer?.cancel();
        setState(() {
          _isDone = true;
          _secondsLeft = 0;
        });
        _rotationCtrl.stop();
      }
    });
  }

  @override
  void dispose() {
    _timer?.cancel();
    _rotationCtrl.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Dialog(
      backgroundColor: const Color(0xFF0F0F26),
      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(16)),
      child: Padding(
        padding: const EdgeInsets.all(24.0),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            if (!_isDone) ...[
              RotationTransition(
                turns: _rotationCtrl,
                child: const Icon(
                  Icons.explore_rounded,
                  color: Color(0xFFFF88EE),
                  size: 64,
                ),
              ),
              const SizedBox(height: 20),
              const Text(
                'Calibrando Brújula...',
                style: TextStyle(
                  color: Colors.white,
                  fontWeight: FontWeight.bold,
                  fontSize: 18,
                ),
              ),
              const SizedBox(height: 12),
              const Text(
                'Mueve el reloj en forma de 8 (infinito) en todas las direcciones para recolectar lecturas 3D.',
                textAlign: TextAlign.center,
                style: TextStyle(
                  color: Colors.white70,
                  fontSize: 13,
                  height: 1.4,
                ),
              ),
              const SizedBox(height: 24),
              Stack(
                alignment: Alignment.center,
                children: [
                  SizedBox(
                    width: 72,
                    height: 72,
                    child: CircularProgressIndicator(
                      value: _secondsLeft / 10.0,
                      strokeWidth: 6,
                      valueColor: const AlwaysStoppedAnimation<Color>(Color(0xFFFF88EE)),
                      backgroundColor: Colors.white12,
                    ),
                  ),
                  Text(
                    '${_secondsLeft}s',
                    style: const TextStyle(
                      color: Colors.white,
                      fontSize: 20,
                      fontWeight: FontWeight.bold,
                      fontFamily: 'monospace',
                    ),
                  ),
                ],
              ),
            ] else ...[
              const Icon(
                Icons.check_circle_rounded,
                color: Color(0xFF00FF88),
                size: 64,
              ),
              const SizedBox(height: 20),
              const Text(
                '¡Calibración Completada!',
                style: TextStyle(
                  color: Colors.white,
                  fontWeight: FontWeight.bold,
                  fontSize: 18,
                ),
              ),
              const SizedBox(height: 12),
              const Text(
                'Los nuevos coeficientes de calibración de hierro duro y blando han sido guardados en la memoria flash del reloj.',
                textAlign: TextAlign.center,
                style: TextStyle(
                  color: Colors.white70,
                  fontSize: 13,
                  height: 1.4,
                ),
              ),
              const SizedBox(height: 24),
              SizedBox(
                width: double.infinity,
                child: ElevatedButton(
                  onPressed: () => Navigator.pop(context),
                  style: ElevatedButton.styleFrom(
                    backgroundColor: const Color(0xFF163E2A),
                    foregroundColor: const Color(0xFF00FF88),
                    padding: const EdgeInsets.symmetric(vertical: 14),
                    shape: RoundedRectangleBorder(
                      borderRadius: BorderRadius.circular(10),
                    ),
                  ),
                  child: const Text('Entendido'),
                ),
              ),
            ],
          ],
        ),
      ),
    );
  }
}

