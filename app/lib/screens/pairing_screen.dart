import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../repositories/partner_repository.dart';
import '../services/ble_service.dart';

/// Pantalla de emparejamiento BLE.
///
/// Permite:
/// - Escanear dispositivos BLE cercanos
/// - Seleccionar y conectar al reloj "Jessi Watch"
/// - Ver el estado de la conexión
/// - Desconectar
class PairingScreen extends StatefulWidget {
  const PairingScreen({super.key});

  @override
  State<PairingScreen> createState() => _PairingScreenState();
}

class _PairingScreenState extends State<PairingScreen>
    with SingleTickerProviderStateMixin {
  late AnimationController _scanAnimController;

  @override
  void initState() {
    super.initState();
    _scanAnimController = AnimationController(
      vsync: this,
      duration: const Duration(seconds: 2),
    );
  }

  @override
  void dispose() {
    _scanAnimController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final bleService = context.watch<BleService>();
    final isConnected =
        bleService.connectionState == BleConnectionState.connected;

    return Scaffold(
      backgroundColor: const Color(0xFF0A0A1A),
      appBar: AppBar(
        backgroundColor: Colors.transparent,
        elevation: 0,
        title: const Text(
          'Emparejar Reloj',
          style: TextStyle(fontWeight: FontWeight.w700, letterSpacing: -0.5),
        ),
      ),
      body: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 20),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            const SizedBox(height: 16),

            // ── Estado actual ──────────────────────────────────────────
            _buildConnectionStatus(bleService),

            const SizedBox(height: 24),

            // ── Botones de acción ──────────────────────────────────────
            if (!isConnected) _buildScanButton(bleService),
            if (isConnected) _buildDisconnectButton(bleService),

            const SizedBox(height: 24),

            // ── Lista de dispositivos ──────────────────────────────────
            if (bleService.isScanning || bleService.discoveredDevices.isNotEmpty)
              _buildDeviceListHeader(bleService),

            Expanded(
              child: bleService.discoveredDevices.isEmpty
                  ? _buildEmptyState(bleService)
                  : _buildDeviceList(bleService),
            ),

            const SizedBox(height: 16),

            // ── Info ────────────────────────────────────────────────────
            _buildInfoFooter(),

            const SizedBox(height: 24),
          ],
        ),
      ),
    );
  }

  Widget _buildConnectionStatus(BleService bleService) {
    final state = bleService.connectionState;
    final Color statusColor;
    final IconData statusIcon;
    final String statusText;
    final String statusSubtext;

    switch (state) {
      case BleConnectionState.connected:
        statusColor = const Color(0xFF00CC88);
        statusIcon = Icons.bluetooth_connected_rounded;
        statusText = 'Conectado';
        statusSubtext =
            bleService.connectedDeviceId ?? 'Dispositivo desconocido';
        break;
      case BleConnectionState.connecting:
        statusColor = const Color(0xFFFFCC00);
        statusIcon = Icons.bluetooth_searching_rounded;
        statusText = 'Conectando...';
        statusSubtext = 'Estableciendo conexión BLE';
        break;
      case BleConnectionState.disconnecting:
        statusColor = const Color(0xFFFF6644);
        statusIcon = Icons.bluetooth_disabled_rounded;
        statusText = 'Desconectando...';
        statusSubtext = '';
        break;
      case BleConnectionState.disconnected:
        statusColor = const Color(0xFF666680);
        statusIcon = Icons.bluetooth_rounded;
        statusText = 'Desconectado';
        statusSubtext = 'Escanea para encontrar tu reloj';
        break;
    }

    return Container(
      padding: const EdgeInsets.all(20),
      decoration: BoxDecoration(
        gradient: LinearGradient(
          begin: Alignment.topLeft,
          end: Alignment.bottomRight,
          colors: [
            statusColor.withOpacity(0.12),
            statusColor.withOpacity(0.04),
          ],
        ),
        borderRadius: BorderRadius.circular(20),
        border: Border.all(color: statusColor.withOpacity(0.15)),
      ),
      child: Row(
        children: [
          Container(
            width: 56,
            height: 56,
            decoration: BoxDecoration(
              shape: BoxShape.circle,
              color: statusColor.withOpacity(0.15),
            ),
            child: Icon(statusIcon, color: statusColor, size: 28),
          ),
          const SizedBox(width: 16),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  statusText,
                  style: TextStyle(
                    fontSize: 18,
                    fontWeight: FontWeight.w700,
                    color: statusColor,
                  ),
                ),
                if (statusSubtext.isNotEmpty)
                  Text(
                    statusSubtext,
                    style: TextStyle(
                      fontSize: 13,
                      color: Colors.white.withOpacity(0.35),
                    ),
                    overflow: TextOverflow.ellipsis,
                  ),
              ],
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildScanButton(BleService bleService) {
    return AnimatedContainer(
      duration: const Duration(milliseconds: 300),
      height: 52,
      child: ElevatedButton.icon(
        onPressed: bleService.isScanning
            ? () => bleService.stopScan()
            : () {
                bleService.startScan();
                _scanAnimController.repeat();
              },
        style: ElevatedButton.styleFrom(
          backgroundColor: bleService.isScanning
              ? const Color(0xFF2A1A1A)
              : const Color(0xFF1A2A4A),
          foregroundColor: bleService.isScanning
              ? const Color(0xFFFF6644)
              : const Color(0xFF4488FF),
          shape: RoundedRectangleBorder(
            borderRadius: BorderRadius.circular(14),
          ),
          elevation: 0,
        ),
        icon: bleService.isScanning
            ? const SizedBox(
                width: 18,
                height: 18,
                child: CircularProgressIndicator(
                  strokeWidth: 2,
                  color: Color(0xFFFF6644),
                ),
              )
            : const Icon(Icons.bluetooth_searching_rounded),
        label: Text(
          bleService.isScanning ? 'Detener escaneo' : 'Escanear dispositivos',
          style: const TextStyle(fontWeight: FontWeight.w600),
        ),
      ),
    );
  }

  Widget _buildDisconnectButton(BleService bleService) {
    return SizedBox(
      height: 52,
      child: ElevatedButton.icon(
        onPressed: () => bleService.disconnect(),
        style: ElevatedButton.styleFrom(
          backgroundColor: const Color(0xFF2A1A1A),
          foregroundColor: const Color(0xFFFF6644),
          shape: RoundedRectangleBorder(
            borderRadius: BorderRadius.circular(14),
          ),
          elevation: 0,
        ),
        icon: const Icon(Icons.bluetooth_disabled_rounded),
        label: const Text(
          'Desconectar',
          style: TextStyle(fontWeight: FontWeight.w600),
        ),
      ),
    );
  }

  Widget _buildDeviceListHeader(BleService bleService) {
    return Padding(
      padding: const EdgeInsets.only(bottom: 12),
      child: Row(
        mainAxisAlignment: MainAxisAlignment.spaceBetween,
        children: [
          Text(
            'DISPOSITIVOS ENCONTRADOS',
            style: TextStyle(
              fontSize: 11,
              fontWeight: FontWeight.w700,
              letterSpacing: 1.2,
              color: Colors.white.withOpacity(0.3),
            ),
          ),
          Text(
            '${bleService.discoveredDevices.length}',
            style: TextStyle(
              fontSize: 12,
              fontWeight: FontWeight.w600,
              color: Colors.white.withOpacity(0.4),
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildDeviceList(BleService bleService) {
    return ListView.separated(
      itemCount: bleService.discoveredDevices.length,
      separatorBuilder: (_, __) => const SizedBox(height: 8),
      itemBuilder: (context, index) {
        final device = bleService.discoveredDevices[index];
        final isJessi = device.name.contains('Jessi') ||
            device.name.contains('Couples');

        return GestureDetector(
          onTap: () {
            // Conectar al dispositivo
            bleService.connectToDevice(device.id);

            // Guardar el ID para reconexión automática
            final repo = context.read<PartnerRepository>();
            repo.saveDeviceId(device.id);
          },
          child: AnimatedContainer(
            duration: const Duration(milliseconds: 200),
            padding: const EdgeInsets.all(16),
            decoration: BoxDecoration(
              gradient: isJessi
                  ? LinearGradient(
                      colors: [
                        const Color(0xFF4488FF).withOpacity(0.12),
                        const Color(0xFF4488FF).withOpacity(0.04),
                      ],
                    )
                  : null,
              color: isJessi ? null : Colors.white.withOpacity(0.04),
              borderRadius: BorderRadius.circular(14),
              border: Border.all(
                color: isJessi
                    ? const Color(0xFF4488FF).withOpacity(0.2)
                    : Colors.white.withOpacity(0.04),
              ),
            ),
            child: Row(
              children: [
                // Icono
                Container(
                  width: 44,
                  height: 44,
                  decoration: BoxDecoration(
                    shape: BoxShape.circle,
                    color: isJessi
                        ? const Color(0xFF4488FF).withOpacity(0.15)
                        : Colors.white.withOpacity(0.06),
                  ),
                  child: Icon(
                    isJessi ? Icons.watch_rounded : Icons.bluetooth_rounded,
                    color: isJessi
                        ? const Color(0xFF4488FF)
                        : Colors.white.withOpacity(0.4),
                    size: 22,
                  ),
                ),
                const SizedBox(width: 14),

                // Info
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(
                        device.name.isNotEmpty ? device.name : 'Sin nombre',
                        style: TextStyle(
                          fontSize: 15,
                          fontWeight: FontWeight.w600,
                          color: isJessi
                              ? Colors.white
                              : Colors.white.withOpacity(0.6),
                        ),
                      ),
                      Text(
                        device.id,
                        style: TextStyle(
                          fontSize: 11,
                          color: Colors.white.withOpacity(0.25),
                          fontFamily: 'monospace',
                        ),
                      ),
                    ],
                  ),
                ),

                // RSSI
                Column(
                  crossAxisAlignment: CrossAxisAlignment.end,
                  children: [
                    Icon(
                      _rssiIcon(device.rssi),
                      size: 18,
                      color: _rssiColor(device.rssi),
                    ),
                    Text(
                      '${device.rssi} dBm',
                      style: TextStyle(
                        fontSize: 10,
                        color: Colors.white.withOpacity(0.25),
                      ),
                    ),
                  ],
                ),
              ],
            ),
          ),
        );
      },
    );
  }

  Widget _buildEmptyState(BleService bleService) {
    return Center(
      child: Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          Icon(
            bleService.isScanning
                ? Icons.bluetooth_searching_rounded
                : Icons.watch_off_rounded,
            size: 56,
            color: Colors.white.withOpacity(0.1),
          ),
          const SizedBox(height: 16),
          Text(
            bleService.isScanning
                ? 'Buscando dispositivos...'
                : 'Toca "Escanear" para buscar tu reloj',
            style: TextStyle(
              fontSize: 14,
              color: Colors.white.withOpacity(0.25),
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildInfoFooter() {
    return Container(
      padding: const EdgeInsets.all(14),
      decoration: BoxDecoration(
        color: const Color(0xFF1A1A2E),
        borderRadius: BorderRadius.circular(12),
      ),
      child: Row(
        children: [
          Icon(
            Icons.info_outline_rounded,
            size: 18,
            color: Colors.white.withOpacity(0.3),
          ),
          const SizedBox(width: 10),
          Expanded(
            child: Text(
              'Asegúrate de que el reloj está encendido y que el Bluetooth '
              'está activado en tu dispositivo.',
              style: TextStyle(
                fontSize: 12,
                color: Colors.white.withOpacity(0.25),
                height: 1.4,
              ),
            ),
          ),
        ],
      ),
    );
  }

  // ── Helpers ────────────────────────────────────────────────────────────

  IconData _rssiIcon(int rssi) {
    if (rssi > -50) return Icons.signal_cellular_4_bar_rounded;
    if (rssi > -65) return Icons.signal_cellular_alt_rounded;
    if (rssi > -80) return Icons.signal_cellular_alt_2_bar_rounded;
    return Icons.signal_cellular_alt_1_bar_rounded;
  }

  Color _rssiColor(int rssi) {
    if (rssi > -50) return const Color(0xFF00CC88);
    if (rssi > -65) return const Color(0xFF88CC00);
    if (rssi > -80) return const Color(0xFFFFCC00);
    return const Color(0xFFFF6644);
  }
}
