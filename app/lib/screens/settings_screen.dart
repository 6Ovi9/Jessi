import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../models/config_model.dart';
import '../repositories/partner_repository.dart';
import '../services/ble_service.dart';
import '../widgets/watch_preview_widget.dart';

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

    // Guardar en Supabase
    await repo.saveConfig(_config);

    // Enviar al reloj por BLE
    await bleService.writeConfig(_config);

    setState(() => _hasChanges = false);

    if (mounted) {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: const Text('Configuración guardada'),
          backgroundColor: const Color(0xFF1A2A3A),
          behavior: SnackBarBehavior.floating,
          shape: RoundedRectangleBorder(
            borderRadius: BorderRadius.circular(8),
          ),
        ),
      );
    }
  }

  @override
  Widget build(BuildContext context) {
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
            ),
            _buildColorTile(
              'Minutos',
              _config.minutesConnectedColor,
              _config.colorMinutesConnected,
            ),
            _buildColorTile(
              'Segundos',
              _config.secondsConnectedColor,
              _config.colorSecondsConnected,
            ),
          ]),

          // ── Colores (Desconectado) ──────────────────────────────────
          _buildSection('Colores — Desconectado', [
            _buildColorTile(
              'Horas',
              _config.hoursDiscColor,
              _config.colorHoursDisc,
            ),
            _buildColorTile(
              'Minutos',
              _config.minutesDiscColor,
              _config.colorMinutesDisc,
            ),
            _buildColorTile(
              'Segundos',
              _config.secondsDiscColor,
              _config.colorSecondsDisc,
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
              '0x${_config.wakeThreshold.toRadixString(16).padLeft(2, '0').toUpperCase()}',
              _config.wakeThreshold.toDouble(),
              0,
              10,
              (value) => _updateConfig(
                  (c) => c.copyWith(wakeThreshold: value.round())),
              activeColor: const Color(0xFF8866FF),
            ),
            Padding(
              padding:
                  const EdgeInsets.symmetric(horizontal: 16, vertical: 4),
              child: Text(
                'Menor valor = más sensible. Default: 0x02 (~312mg).\n'
                'Si despierta solo: subir. Si no detecta: bajar.',
                style: TextStyle(
                  fontSize: 11,
                  color: Colors.white.withValues(alpha: 0.25),
                  height: 1.4,
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

          const SizedBox(height: 40),
        ],
      ),
    );
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

  Widget _buildColorTile(String label, Color color, String hexValue) {
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 10),
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
            '#$hexValue',
            style: TextStyle(
              fontSize: 12,
              fontFamily: 'monospace',
              color: Colors.white.withValues(alpha: 0.3),
            ),
          ),
        ],
      ),
    );
  }
}
