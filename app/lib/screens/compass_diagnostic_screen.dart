import 'dart:math';
import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../services/ble_service.dart';

class CompassDiagnosticScreen extends StatefulWidget {
  const CompassDiagnosticScreen({super.key});

  @override
  State<CompassDiagnosticScreen> createState() => _CompassDiagnosticScreenState();
}

class _CompassDiagnosticScreenState extends State<CompassDiagnosticScreen> with WidgetsBindingObserver {
  late BleService _bleService;
  bool _isStreaming = false;
  bool _isCalibrating = false;
  int _calibSecondsLeft = 15;

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addObserver(this);
    _bleService = context.read<BleService>();
    _startStream();
  }

  @override
  void dispose() {
    WidgetsBinding.instance.removeObserver(this);
    _stopStream();
    super.dispose();
  }

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    if (state == AppLifecycleState.resumed) {
      _startStream();
    } else if (state == AppLifecycleState.paused) {
      _stopStream();
    }
  }

  Future<void> _startStream() async {
    if (!_isStreaming) {
      _isStreaming = true;
      await _bleService.startCompassStream();
    }
  }

  Future<void> _stopStream() async {
    if (_isStreaming) {
      _isStreaming = false;
      await _bleService.stopCompassStream();
    }
  }

  void _triggerCompassCalibration() async {
    if (_isCalibrating) return;
    setState(() {
      _isCalibrating = true;
      _calibSecondsLeft = 15;
    });

    await _bleService.startCompassCalibration();

    for (int i = 15; i > 0; i--) {
      if (!mounted) return;
      setState(() {
        _calibSecondsLeft = i;
      });
      await Future.delayed(const Duration(seconds: 1));
    }

    if (mounted) {
      setState(() {
        _isCalibrating = false;
      });
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
          content: Text('¡Calibración de brújula completada y guardada en el reloj!'),
          backgroundColor: Colors.green,
        ),
      );
    }
  }

  @override
  Widget build(BuildContext context) {
    final isConnected = context.select<BleService, bool>((ble) => ble.connectionState == BleConnectionState.connected);

    return Scaffold(
      backgroundColor: const Color(0xFF0A0A1A),
      appBar: AppBar(
        title: const Text('Brújula y Calibración LIS3MDL'),
        backgroundColor: Colors.transparent,
      ),
      body: Center(
        child: isConnected 
          ? SingleChildScrollView(
              child: Column(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  const Text('Apunta el reloj y verifica el norte', style: TextStyle(color: Colors.white54)),
                  const SizedBox(height: 30),
                  StreamBuilder<double>(
                    stream: _bleService.compassStream,
                    builder: (context, snapshot) {
                      final heading = snapshot.data ?? 0.0;
                      return Column(
                        children: [
                          Transform.rotate(
                            angle: -heading * pi / 180,
                            child: const Icon(Icons.navigation, size: 140, color: Colors.redAccent),
                          ),
                          const SizedBox(height: 20),
                          Text('${heading.toStringAsFixed(1)}°', style: const TextStyle(fontSize: 32, fontWeight: FontWeight.bold, color: Colors.white)),
                        ],
                      );
                    },
                  ),
                  const SizedBox(height: 40),
                  Padding(
                    padding: const EdgeInsets.symmetric(horizontal: 24),
                    child: Column(
                      children: [
                        if (_isCalibrating) ...[
                          Container(
                            padding: const EdgeInsets.all(16),
                            decoration: BoxDecoration(
                              color: const Color(0xFF1E1428),
                              borderRadius: BorderRadius.circular(16),
                              border: Border.all(color: const Color(0xFFFF88EE), width: 1.5),
                            ),
                            child: Column(
                              children: [
                                const Icon(Icons.sync_rounded, color: Color(0xFFFF88EE), size: 36),
                                const SizedBox(height: 8),
                                Text(
                                  'Gira el reloj haciendo un 8 en el aire\n$_calibSecondsLeft s restantes',
                                  textAlign: TextAlign.center,
                                  style: const TextStyle(color: Colors.white, fontWeight: FontWeight.w700, fontSize: 15),
                                ),
                                const SizedBox(height: 12),
                                LinearProgressIndicator(
                                  value: (15 - _calibSecondsLeft) / 15.0,
                                  backgroundColor: Colors.white10,
                                  color: const Color(0xFFFF88EE),
                                ),
                              ],
                            ),
                          ),
                        ] else ...[
                          ElevatedButton.icon(
                            onPressed: _triggerCompassCalibration,
                            icon: const Icon(Icons.explore_rounded, size: 20),
                            label: const Text('Calibrar Brújula (15 seg)', style: TextStyle(fontWeight: FontWeight.bold)),
                            style: ElevatedButton.styleFrom(
                              backgroundColor: const Color(0xFF4488FF),
                              foregroundColor: Colors.white,
                              padding: const EdgeInsets.symmetric(horizontal: 24, vertical: 16),
                              shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
                            ),
                          ),
                          const SizedBox(height: 10),
                          const Text(
                            'Gira el reloj en todas las direcciones para compensar interferencias magnéticas.',
                            textAlign: TextAlign.center,
                            style: TextStyle(color: Colors.white38, fontSize: 12),
                          ),
                        ],
                      ],
                    ),
                  ),
                ],
              ),
            )
          : const Text('Reloj desconectado', style: TextStyle(color: Colors.redAccent)),
      ),
    );
  }
}
