import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_foreground_task/flutter_foreground_task.dart';
import 'package:latlong2/latlong.dart';
import 'package:provider/provider.dart';
import 'package:shared_preferences/shared_preferences.dart';
import 'package:supabase_flutter/supabase_flutter.dart';

import 'models/config_model.dart';
import 'repositories/partner_repository.dart';
import 'screens/home_screen.dart';
import 'services/bearing_calculator.dart';
import 'services/ble_service.dart';
import 'services/foreground_service.dart' as fg;
import 'services/location_service.dart';
import 'services/sync_service.dart';

// ════════════════════════════════════════════════════════════════════════════
// CONFIGURACIÓN — Modificar estos valores según tu instalación
// ════════════════════════════════════════════════════════════════════════════

/// URL del backend Supabase.
/// Cambiar a la IP de Tailscale de tu PC: http://100.x.x.x:8000
const String supabaseUrl = 'http://100.103.87.29:8000';

/// Anon key de Supabase (generada a partir del JWT_SECRET del backend).
const String supabaseAnonKey =
    'eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJyb2xlIjoiYW5vbiIsImlzcyI6InN1cGFiYXNlIiwiaWF0IjoxNzAwMDAwMDAwLCJleHAiOjIwMDAwMDAwMDB9.3B1Uvi60MBpMQhXe8MAXU8oyuByp6sZJKib_8mYj3jw';

/// ID del usuario actual. Cambiar a 'B' en el teléfono de la pareja.
const String myUserId = 'A';

// ════════════════════════════════════════════════════════════════════════════

void main() async {
  WidgetsFlutterBinding.ensureInitialized();

  // Lock portrait orientation
  await SystemChrome.setPreferredOrientations([
    DeviceOrientation.portraitUp,
  ]);

  // Status bar transparente
  SystemChrome.setSystemUIOverlayStyle(const SystemUiOverlayStyle(
    statusBarColor: Colors.transparent,
    statusBarIconBrightness: Brightness.light,
    systemNavigationBarColor: Color(0xFF0A0A1A),
    systemNavigationBarIconBrightness: Brightness.light,
  ));

  // Inicializar Supabase
  try {
    await Supabase.initialize(
      url: supabaseUrl,
      anonKey: supabaseAnonKey,
    );
    debugPrint('[MAIN] Supabase initialized OK');
  } catch (e) {
    debugPrint('[MAIN] Supabase.initialize() error: $e');
  }

  // Inicializar Foreground Service
  fg.ForegroundService.initialize();

  runApp(const CouplesWatchApp());
}

class CouplesWatchApp extends StatelessWidget {
  const CouplesWatchApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MultiProvider(
      providers: [
        ChangeNotifierProvider(create: (_) => BleService()..initialize()),
        ChangeNotifierProvider(create: (_) => LocationService()),
        ChangeNotifierProvider(create: (_) => SyncService()),
        ChangeNotifierProvider(create: (_) => PartnerRepository()),
      ],
      child: MaterialApp(
        title: 'Couples Watch',
        debugShowCheckedModeBanner: false,
        theme: ThemeData(
          brightness: Brightness.dark,
          scaffoldBackgroundColor: const Color(0xFF0A0A1A),
          colorScheme: const ColorScheme.dark(
            primary: Color(0xFF4488FF),
            secondary: Color(0xFF00CC88),
            surface: Color(0xFF1A1A2E),
            error: Color(0xFFFF4444),
          ),
          appBarTheme: const AppBarTheme(
            backgroundColor: Color(0xFF0A0A1A),
            elevation: 0,
            centerTitle: false,
          ),
          fontFamily: 'sans-serif',
          useMaterial3: true,
        ),
        home: WithForegroundTask(
          child: const AppBootstrapper(),
        ),
      ),
    );
  }
}

/// Widget que inicializa todos los servicios al arrancar la app.
///
/// Se encarga de:
/// 1. Inicializar Supabase Sync
/// 2. Iniciar el Foreground Service
/// 3. Iniciar el GPS
/// 4. Conectar los callbacks entre servicios
/// 5. Auto-reconectar BLE si hay un dispositivo guardado
class AppBootstrapper extends StatefulWidget {
  const AppBootstrapper({super.key});

  @override
  State<AppBootstrapper> createState() => _AppBootstrapperState();
}

class _AppBootstrapperState extends State<AppBootstrapper> {
  bool _initialized = false;
  String? _bootError;
  String _bootStep = 'Inicializando...';

  @override
  void initState() {
    super.initState();
    _bootstrap();
  }

  Future<void> _bootstrap() async {
    setState(() {
      _bootError = null;
      _bootStep = 'Inicializando...';
      _initialized = false;
    });

    final bleService = context.read<BleService>();
    final locationService = context.read<LocationService>();
    final syncService = context.read<SyncService>();
    final partnerRepo = context.read<PartnerRepository>();

    print('[APP] Bootstrapping...');

    try {
      // 1. Inicializar repositorio y sync (con timeout)
      setState(() => _bootStep = 'Conectando al servidor...');
      await partnerRepo.initialize(myUserId)
          .timeout(const Duration(seconds: 8));
      
      setState(() => _bootStep = 'Sincronizando...');
      await syncService.initialize(myUserId)
          .timeout(const Duration(seconds: 8));

      // 2. Cargar configuración y aplicar intervalos de polling
      setState(() => _bootStep = 'Cargando configuración...');
      final config = partnerRepo.config ?? WatchConfig.defaultFor(myUserId);
      locationService.setPollingIntervals(
        precisionS: config.gpsIntervalPrecisionS,
        nearS: config.gpsIntervalNearS,
        farS: config.gpsIntervalFarS,
        remoteMinS: config.gpsIntervalRemoteMinS,
        remoteMaxS: config.gpsIntervalRemoteMaxS,
      );

      // 3. Conectar callbacks entre servicios

      // GPS → Supabase: subir ubicación propia
      locationService.onLocationUpdate = (position, bearing, distanceM) {
        syncService.uploadLocation(position, locationService.currentMode.label);

        // GPS → BLE: enviar bearing y distancia al reloj
        if (bleService.connectionState == BleConnectionState.connected) {
          bleService.writeBearing(bearing);
          bleService.writeDistance(distanceM);
        }

        // Actualizar notificación del Foreground Service
        fg.ForegroundService.updateNotification(
          text: locationService.statusDescription,
        );
      };

      // Supabase → GPS: actualizar ubicación de la pareja
      syncService.onPartnerLocationUpdate = (LatLng partnerPos) {
        locationService.updatePartnerLocation(partnerPos);
      };

      // Supabase → BLE: evento háptico recibido → vibrar reloj
      syncService.onHapticEventReceived = () {
        bleService.sendHapticCommand();
      };

      // BLE → Supabase: reloj notifica toque → enviar a pareja
      bleService.onHapticTxReceived = () {
        syncService.sendHapticEvent();
      };

      // BLE → GPS: reloj entra/sale de RADAR_MODE → ajustar polling
      bleService.onRadarModeChanged = (bool active) {
        locationService.setRadarModeActive(active);
      };

      // 4. Obtener última ubicación conocida de la pareja
      try {
        final lastPartnerLoc = await syncService.fetchPartnerLocation()
            .timeout(const Duration(seconds: 5));
        if (lastPartnerLoc != null && lastPartnerLoc.isValid) {
          locationService.updatePartnerLocation(
            LatLng(lastPartnerLoc.latitude, lastPartnerLoc.longitude),
          );
        }
      } catch (e) {
        print('[APP] Could not fetch partner location: $e');
      }

      // 5. Iniciar Foreground Service
      setState(() => _bootStep = 'Iniciando servicios...');
      await fg.ForegroundService.start();

      // 6. Iniciar GPS
      await locationService.start();

      // 7. Auto-reconectar BLE si hay dispositivo guardado
      final prefs = await SharedPreferences.getInstance();
      final savedDeviceId = prefs.getString('ble_device_id');
      if (savedDeviceId != null) {
        print('[APP] Auto-reconnecting to saved device: $savedDeviceId');
        bleService.connectToDevice(savedDeviceId);
      }

      print('[APP] Bootstrap complete!');
    } catch (e, stack) {
      print('[APP] Bootstrap error: $e');
      print('[APP] Stack: $stack');
      setState(() => _bootError = e.toString());
    }

    setState(() => _initialized = true);
  }

  @override
  Widget build(BuildContext context) {
    if (!_initialized) {
      return Scaffold(
        backgroundColor: const Color(0xFF0A0A1A),
        body: Center(
          child: Column(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              const SizedBox(
                width: 48,
                height: 48,
                child: CircularProgressIndicator(
                  strokeWidth: 2.5,
                  color: Color(0xFF4488FF),
                ),
              ),
              const SizedBox(height: 24),
              const Text(
                'Couples Watch',
                style: TextStyle(
                  fontSize: 22,
                  fontWeight: FontWeight.w700,
                  color: Colors.white,
                  letterSpacing: -0.5,
                ),
              ),
              const SizedBox(height: 8),
              Text(
                _bootStep,
                style: const TextStyle(
                  fontSize: 14,
                  color: Color(0xFF666680),
                ),
              ),
              if (_bootError != null) ...[
                const SizedBox(height: 16),
                Padding(
                  padding: const EdgeInsets.symmetric(horizontal: 32),
                  child: Text(
                    '⚠️ $_bootError',
                    style: const TextStyle(fontSize: 12, color: Colors.redAccent),
                    textAlign: TextAlign.center,
                  ),
                ),
              ],
            ],
          ),
        ),
      );
    }

    // Mostrar error como snackbar si hubo uno durante bootstrap
    if (_bootError != null) {
      WidgetsBinding.instance.addPostFrameCallback((_) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text('⚠️ Error de inicio: $_bootError'),
            duration: const Duration(seconds: 5),
            backgroundColor: Colors.red.shade800,
            action: SnackBarAction(
              label: 'Reintentar',
              textColor: Colors.white,
              onPressed: () => _bootstrap(),
            ),
          ),
        );
        _bootError = null; // Solo mostrar una vez
      });
    }

    return const HomeScreen();
  }
}
