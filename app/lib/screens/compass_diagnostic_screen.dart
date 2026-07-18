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

  @override
  Widget build(BuildContext context) {
    final isConnected = context.select<BleService, bool>((ble) => ble.connectionState == BleConnectionState.connected);

    return Scaffold(
      backgroundColor: const Color(0xFF0A0A1A),
      appBar: AppBar(
        title: const Text('Brújula del Reloj (En vivo)'),
        backgroundColor: Colors.transparent,
      ),
      body: Center(
        child: isConnected 
          ? Column(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                const Text('Apunta el reloj y verifica el norte', style: TextStyle(color: Colors.white54)),
                const SizedBox(height: 40),
                StreamBuilder<double>(
                  stream: _bleService.compassStream,
                  builder: (context, snapshot) {
                    final heading = snapshot.data ?? 0.0;
                    return Column(
                      children: [
                        Transform.rotate(
                          angle: -heading * pi / 180,
                          child: Icon(Icons.navigation, size: 150, color: Colors.redAccent),
                        ),
                        const SizedBox(height: 30),
                        Text('${heading.toStringAsFixed(1)}°', style: const TextStyle(fontSize: 32, fontWeight: FontWeight.bold, color: Colors.white)),
                      ],
                    );
                  },
                ),
              ],
            )
          : const Text('Reloj desconectado', style: TextStyle(color: Colors.redAccent)),
      ),
    );
  }
}
