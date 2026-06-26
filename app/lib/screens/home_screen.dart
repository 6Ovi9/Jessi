import 'dart:math';

import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../models/config_model.dart';
import '../services/bearing_calculator.dart';
import '../services/ble_service.dart';
import '../services/location_service.dart';
import '../services/sync_service.dart';
import '../widgets/watch_preview_widget.dart';
import 'settings_screen.dart';
import 'pairing_screen.dart';

/// Pantalla principal de la app.
///
/// Muestra:
/// - Previsualización del reloj en tiempo real
/// - Estado de conexión BLE
/// - Distancia y dirección hacia la pareja
/// - Modo de polling GPS actual
/// - Batería del reloj
class HomeScreen extends StatefulWidget {
  const HomeScreen({super.key});

  @override
  State<HomeScreen> createState() => _HomeScreenState();
}

class _HomeScreenState extends State<HomeScreen> with TickerProviderStateMixin {
  WatchPreviewMode _previewMode = WatchPreviewMode.clock;
  late AnimationController _pulseController;

  @override
  void initState() {
    super.initState();
    _pulseController = AnimationController(
      vsync: this,
      duration: const Duration(milliseconds: 2000),
    )..repeat(reverse: true);
  }

  @override
  void dispose() {
    _pulseController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final bleService = context.watch<BleService>();
    final locationService = context.watch<LocationService>();
    final syncService = context.watch<SyncService>();

    final isConnected = bleService.connectionState == BleConnectionState.connected;
    final distanceKm = locationService.distanceKm;
    final bearing = locationService.bearingDeg;

    return Scaffold(
      backgroundColor: const Color(0xFF0A0A1A),
      body: SafeArea(
        child: Column(
          children: [
            // ── Header ───────────────────────────────────────────────────
            _buildHeader(context, bleService),

            // ── Watch Preview ────────────────────────────────────────────
            Expanded(
              flex: 5,
              child: Center(
                child: GestureDetector(
                  onDoubleTap: () {
                    // Ciclar entre modos de preview
                    setState(() {
                      _previewMode = WatchPreviewMode.values[
                          (_previewMode.index + 1) %
                              WatchPreviewMode.values.length];
                    });
                  },
                  child: Hero(
                    tag: 'watch_preview',
                    child: WatchPreviewWidget(
                      mode: _previewMode,
                      config: WatchConfig.defaultFor('A'),
                      isConnected: isConnected,
                      bearing: bearing,
                      distanceKm: distanceKm >= 0 ? distanceKm : 0,
                      size: MediaQuery.of(context).size.width * 0.65,
                    ),
                  ),
                ),
              ),
            ),

            // ── Mode Selector ────────────────────────────────────────────
            _buildModeSelector(),

            const SizedBox(height: 16),

            // ── Info Cards ───────────────────────────────────────────────
            Expanded(
              flex: 4,
              child: Padding(
                padding: const EdgeInsets.symmetric(horizontal: 20),
                child: Column(
                  children: [
                    // Distancia y dirección
                    _buildDistanceCard(distanceKm, bearing, isConnected),
                    const SizedBox(height: 12),
                    // Estado del sistema
                    _buildStatusCard(
                      bleService, locationService, syncService),
                  ],
                ),
              ),
            ),

            const SizedBox(height: 16),
          ],
        ),
      ),
    );
  }

  Widget _buildHeader(BuildContext context, BleService bleService) {
    final isConnected =
        bleService.connectionState == BleConnectionState.connected;

    return Padding(
      padding: const EdgeInsets.fromLTRB(20, 12, 20, 0),
      child: Row(
        mainAxisAlignment: MainAxisAlignment.spaceBetween,
        children: [
          // Título
          Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(
                'Couples Watch',
                style: TextStyle(
                  fontSize: 22,
                  fontWeight: FontWeight.w700,
                  color: Colors.white.withValues(alpha: 0.95),
                  letterSpacing: -0.5,
                ),
              ),
              const SizedBox(height: 2),
              Row(
                children: [
                  AnimatedBuilder(
                    animation: _pulseController,
                    builder: (context, child) {
                      return Container(
                        width: 8,
                        height: 8,
                        decoration: BoxDecoration(
                          shape: BoxShape.circle,
                          color: isConnected
                              ? Color.lerp(
                                  const Color(0xFF00CC44),
                                  const Color(0xFF00FF66),
                                  _pulseController.value,
                                )
                              : Colors.red.withValues(alpha: 0.7),
                        ),
                      );
                    },
                  ),
                  const SizedBox(width: 6),
                  Text(
                    isConnected ? 'Conectado' : 'Desconectado',
                    style: TextStyle(
                      fontSize: 13,
                      color: isConnected
                          ? const Color(0xFF00CC88)
                          : Colors.red.withValues(alpha: 0.7),
                      fontWeight: FontWeight.w500,
                    ),
                  ),
                  if (isConnected && bleService.batteryPercent >= 0) ...[
                    const SizedBox(width: 12),
                    Icon(
                      _batteryIcon(bleService.batteryPercent),
                      size: 16,
                      color: _batteryColor(bleService.batteryPercent),
                    ),
                    const SizedBox(width: 4),
                    Text(
                      '${bleService.batteryPercent}%',
                      style: TextStyle(
                        fontSize: 13,
                        color: _batteryColor(bleService.batteryPercent),
                        fontWeight: FontWeight.w500,
                      ),
                    ),
                  ],
                ],
              ),
            ],
          ),

          // Acciones
          Row(
            children: [
              // Botón de configuración
              IconButton(
                onPressed: () {
                  Navigator.push(
                    context,
                    MaterialPageRoute(
                      builder: (_) => const SettingsScreen(),
                    ),
                  );
                },
                icon: Icon(
                  Icons.tune_rounded,
                  color: Colors.white.withValues(alpha: 0.7),
                ),
              ),
              // Botón de emparejamiento
              IconButton(
                onPressed: () {
                  Navigator.push(
                    context,
                    MaterialPageRoute(
                      builder: (_) => const PairingScreen(),
                    ),
                  );
                },
                icon: Icon(
                  Icons.bluetooth_searching_rounded,
                  color: isConnected
                      ? const Color(0xFF4488FF)
                      : Colors.white.withValues(alpha: 0.5),
                ),
              ),
            ],
          ),
        ],
      ),
    );
  }

  Widget _buildModeSelector() {
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 40),
      child: Container(
        padding: const EdgeInsets.all(4),
        decoration: BoxDecoration(
          color: Colors.white.withValues(alpha: 0.06),
          borderRadius: BorderRadius.circular(12),
        ),
        child: Row(
          children: WatchPreviewMode.values.map((mode) {
            final isSelected = mode == _previewMode;
            return Expanded(
              child: GestureDetector(
                onTap: () => setState(() => _previewMode = mode),
                child: AnimatedContainer(
                  duration: const Duration(milliseconds: 200),
                  padding: const EdgeInsets.symmetric(vertical: 8),
                  decoration: BoxDecoration(
                    color: isSelected
                        ? Colors.white.withValues(alpha: 0.12)
                        : Colors.transparent,
                    borderRadius: BorderRadius.circular(8),
                  ),
                  child: Text(
                    _modeLabel(mode),
                    textAlign: TextAlign.center,
                    style: TextStyle(
                      fontSize: 13,
                      fontWeight:
                          isSelected ? FontWeight.w600 : FontWeight.w400,
                      color: isSelected
                          ? Colors.white
                          : Colors.white.withValues(alpha: 0.4),
                    ),
                  ),
                ),
              ),
            );
          }).toList(),
        ),
      ),
    );
  }

  Widget _buildDistanceCard(
      double distanceKm, double bearing, bool isConnected) {
    return Container(
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        gradient: LinearGradient(
          begin: Alignment.topLeft,
          end: Alignment.bottomRight,
          colors: [
            Colors.white.withValues(alpha: 0.08),
            Colors.white.withValues(alpha: 0.04),
          ],
        ),
        borderRadius: BorderRadius.circular(16),
        border: Border.all(
          color: Colors.white.withValues(alpha: 0.06),
        ),
      ),
      child: Row(
        children: [
          // Icono de dirección
          Container(
            width: 48,
            height: 48,
            decoration: const BoxDecoration(
              shape: BoxShape.circle,
              color: Color(0xFF1A2A4A),
            ),
            child: distanceKm >= 0
                ? Transform.rotate(
                    angle: bearing * pi / 180,
                    child: const Icon(
                      Icons.navigation_rounded,
                      color: Color(0xFF4488FF),
                      size: 24,
                    ),
                  )
                : const Icon(
                    Icons.location_off_rounded,
                    color: Color(0xFF666680),
                    size: 24,
                  ),
          ),
          const SizedBox(width: 16),

          // Distancia
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  distanceKm >= 0
                      ? _formatDistance(distanceKm)
                      : 'Sin datos',
                  style: const TextStyle(
                    fontSize: 24,
                    fontWeight: FontWeight.w700,
                    color: Colors.white,
                    letterSpacing: -0.5,
                  ),
                ),
                Text(
                  distanceKm >= 0
                      ? 'Bearing: ${bearing.toStringAsFixed(0)}°'
                      : 'Esperando ubicación de la pareja',
                  style: TextStyle(
                    fontSize: 13,
                    color: Colors.white.withValues(alpha: 0.4),
                  ),
                ),
              ],
            ),
          ),

          // Indicador de color de distancia
          if (distanceKm >= 0)
            Container(
              width: 12,
              height: 12,
              decoration: BoxDecoration(
                shape: BoxShape.circle,
                color: _distanceColor(distanceKm),
                boxShadow: [
                  BoxShadow(
                    color: _distanceColor(distanceKm).withValues(alpha: 0.4),
                    blurRadius: 8,
                  ),
                ],
              ),
            ),
        ],
      ),
    );
  }

  Widget _buildStatusCard(
    BleService bleService,
    LocationService locationService,
    SyncService syncService,
  ) {
    return Container(
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: Colors.white.withValues(alpha: 0.04),
        borderRadius: BorderRadius.circular(16),
        border: Border.all(
          color: Colors.white.withValues(alpha: 0.04),
        ),
      ),
      child: Column(
        children: [
          _statusRow(
            Icons.gps_fixed_rounded,
            'GPS',
            locationService.statusDescription,
            _pollingModeColor(locationService.currentMode),
          ),
          const SizedBox(height: 8),
          _statusRow(
            Icons.bluetooth_rounded,
            'BLE',
            bleService.connectionState == BleConnectionState.connected
                ? 'Conectado (${bleService.connectedDeviceId?.substring(0, 8) ?? "?"}...)'
                : bleService.connectionState.name,
            bleService.connectionState == BleConnectionState.connected
                ? const Color(0xFF00CC88)
                : const Color(0xFF666680),
          ),
          const SizedBox(height: 8),
          _statusRow(
            Icons.cloud_sync_rounded,
            'Sync',
            syncService.isConnected
                ? 'Supabase conectado'
                : 'Desconectado',
            syncService.isConnected
                ? const Color(0xFF00CC88)
                : const Color(0xFF666680),
          ),
        ],
      ),
    );
  }

  Widget _statusRow(
      IconData icon, String label, String value, Color statusColor) {
    return Row(
      children: [
        Icon(icon, size: 16, color: statusColor),
        const SizedBox(width: 8),
        Text(
          label,
          style: TextStyle(
            fontSize: 12,
            fontWeight: FontWeight.w600,
            color: Colors.white.withValues(alpha: 0.5),
          ),
        ),
        const SizedBox(width: 8),
        Expanded(
          child: Text(
            value,
            style: TextStyle(
              fontSize: 12,
              color: Colors.white.withValues(alpha: 0.35),
            ),
            overflow: TextOverflow.ellipsis,
          ),
        ),
      ],
    );
  }

  // ── Helpers ────────────────────────────────────────────────────────────

  String _modeLabel(WatchPreviewMode mode) {
    switch (mode) {
      case WatchPreviewMode.clock:
        return 'Reloj';
      case WatchPreviewMode.radar:
        return 'Radar';
      case WatchPreviewMode.distance:
        return 'Distancia';
    }
  }

  String _formatDistance(double km) {
    if (km < 1) {
      return '${(km * 1000).round()} m';
    } else if (km < 100) {
      return '${km.toStringAsFixed(1)} km';
    } else {
      return '${km.round()} km';
    }
  }

  Color _distanceColor(double km) {
    if (km < 15) return const Color(0xFF0080FF);
    if (km < 50) return const Color(0xFF00CC44);
    if (km < 150) return const Color(0xFFFFCC00);
    if (km < 350) return const Color(0xFFFF6600);
    return const Color(0xFFFF0000);
  }

  Color _pollingModeColor(GpsPollingMode mode) {
    switch (mode) {
      case GpsPollingMode.precision:
        return const Color(0xFF00CCFF);
      case GpsPollingMode.near:
        return const Color(0xFF00CC88);
      case GpsPollingMode.far:
        return const Color(0xFFFFCC00);
      case GpsPollingMode.remote:
        return const Color(0xFFFF6600);
    }
  }

  IconData _batteryIcon(int percent) {
    if (percent > 80) return Icons.battery_full_rounded;
    if (percent > 60) return Icons.battery_5_bar_rounded;
    if (percent > 40) return Icons.battery_4_bar_rounded;
    if (percent > 20) return Icons.battery_2_bar_rounded;
    return Icons.battery_alert_rounded;
  }

  Color _batteryColor(int percent) {
    if (percent > 20) return const Color(0xFF00CC88);
    if (percent > 10) return const Color(0xFFFFCC00);
    return const Color(0xFFFF4444);
  }
}
