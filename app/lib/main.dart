import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_dotenv/flutter_dotenv.dart';
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
final String supabaseUrl = dotenv.env['SUPABASE_URL']!;

/// Anon key de Supabase.
/// IMPORTANTE: No puedes usar el JWT_SECRET directamente aquí. Debes generar un JWT
/// firmado con ese secreto que contenga el rol "anon".
/// Ejemplo usando Node.js (jsonwebtoken):
/// jwt.sign({ role: 'anon', iss: 'supabase' }, 'TU_JWT_SECRET', { expiresIn: '10y' })
final String supabaseAnonKey = dotenv.env['SUPABASE_ANON_KEY']!;

// El ID de usuario actual se determina dinámicamente desde SharedPreferences.

// ════════════════════════════════════════════════════════════════════════════

void main() async {
  WidgetsFlutterBinding.ensureInitialized();
  
  await dotenv.load(fileName: '.env');

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
        title: 'Nexus Halo',
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
        home: const WithForegroundTask(
          child: AppBootstrapper(),
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
  String? _selectedUserRole;
  String? _tempSelectedRole;
  bool _showUserRoleSelection = false;

  PartnerRepository? _partnerRepo;
  Future<void>? _bootstrapFuture;

  @override
  void initState() {
    super.initState();
    _partnerRepo = context.read<PartnerRepository>();
    FlutterForegroundTask.receivePort?.listen((message) {
      if (message == 'tick') {
        // El Isolate de UI se despierta brevemente para procesar este mensaje,
        // lo que permite que los timers de Dart (GPS, BLE reconnect) sigan corriendo.
        debugPrint('[APP] Received background tick');
      }
    });
    _bootstrap();
  }

  @override
  void dispose() {
    _partnerRepo?.removeListener(_updateLocationIntervals);
    super.dispose();
  }

  void _updateLocationIntervals() {
    if (!mounted) return;
    if (_selectedUserRole == null) return;
    final partnerRepo = context.read<PartnerRepository>();
    final locationService = context.read<LocationService>();
    final config = partnerRepo.config ?? WatchConfig.defaultFor(_selectedUserRole!);
    locationService.setPollingIntervals(
      precisionS: config.gpsIntervalPrecisionS,
      nearS: config.gpsIntervalNearS,
      farS: config.gpsIntervalFarS,
      remoteMinS: config.gpsIntervalRemoteMinS,
      remoteMaxS: config.gpsIntervalRemoteMaxS,
    );
  }

  Future<void> _bootstrap() {
    if (_bootstrapFuture != null) return _bootstrapFuture!;
    _bootstrapFuture = _doBootstrap().catchError((e) {
      _bootstrapFuture = null;
      throw e;
    });
    return _bootstrapFuture!;
  }

  Future<void> _doBootstrap() async {
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
      final prefs = await SharedPreferences.getInstance();
      if (!mounted) return;
      final role = prefs.getString('user_role');

      if (role == null) {
        setState(() {
          _showUserRoleSelection = true;
          _initialized = true;
        });
        return;
      }

      _selectedUserRole = role;

      // 1. Inicializar repositorio y sync (con timeout)
      setState(() => _bootStep = 'Conectando al servidor...');
      try {
        await partnerRepo.initialize(_selectedUserRole!)
            .timeout(const Duration(seconds: 8));
      } catch (e) {
        print('[APP] partnerRepo init failed (offline mode): $e');
      }
      if (!mounted) return;

      setState(() => _bootStep = 'Sincronizando...');
      try {
        await syncService.initialize(_selectedUserRole!)
            .timeout(const Duration(seconds: 8));
        syncService.cleanupOldHapticEvents();
      } catch (e) {
        print('[APP] syncService init failed (offline mode): $e');
      }
      if (!mounted) return;

      // 2. Cargar configuración y aplicar intervalos de polling
      setState(() => _bootStep = 'Cargando configuración...');
      
      partnerRepo.removeListener(_updateLocationIntervals);
      partnerRepo.addListener(_updateLocationIntervals);
      _updateLocationIntervals();

      // 3. Conectar callbacks entre servicios

      // GPS → Supabase: subir ubicación propia
      locationService.onLocationUpdate = (position, bearing, distanceM) {
        try {
          syncService.uploadLocation(position, locationService.currentMode.label);
        } catch (e) {
          print('[APP] Error uploading location: $e');
        }

        // GPS → BLE: enviar bearing y distancia al reloj
        if (bleService.connectionState == BleConnectionState.connected) {
          bleService.writeBearing(bearing).catchError((e) => print('[BLE] Bearing error: $e'));
          bleService.writeDistance(distanceM).catchError((e) => print('[BLE] Distance error: $e'));
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
      bleService.onRadarModeChanged = (bool active) async {
        await locationService.setRadarModeActive(active);
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
      if (!mounted) return;

      // 5. Iniciar Foreground Service
      setState(() => _bootStep = 'Iniciando servicios...');
      await fg.ForegroundService.start();
      if (!mounted) return;

      // 6. Iniciar GPS
      await locationService.start();
      if (!mounted) return;

      // 7. Auto-reconectar BLE si hay dispositivo guardado
      final savedDeviceId = prefs.getString('ble_device_id');
      if (savedDeviceId != null) {
        print('[APP] Auto-reconnecting to saved device: $savedDeviceId');
        bleService.connectToDevice(savedDeviceId);
      }

      print('[APP] Bootstrap complete!');
    } catch (e, stack) {
      print('[APP] Bootstrap error: $e');
      print('[APP] Stack: $stack');
      if (!mounted) return;
      setState(() {
        _bootError = e.toString();
        _initialized = false;
      });
      return; // Skip setting _initialized = true to block HomeScreen
    }

    if (!mounted) return;
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
                'Nexus Halo',
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
                const SizedBox(height: 16),
                ElevatedButton(
                  onPressed: _bootstrap,
                  child: const Text('Reintentar'),
                ),
              ],
            ],
          ),
        ),
      );
    }

    if (_showUserRoleSelection) {
      return _buildUserSelectionScreen();
    }


    return const HomeScreen();
  }

  Widget _buildUserSelectionScreen() {
    return Scaffold(
      backgroundColor: const Color(0xFF0A0A1A),
      body: SafeArea(
        child: Padding(
          padding: const EdgeInsets.symmetric(horizontal: 24, vertical: 32),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              const Spacer(),
              // Icono / Logo
              Center(
                child: Container(
                  width: 80,
                  height: 80,
                  decoration: BoxDecoration(
                    color: const Color(0xFF4488FF).withValues(alpha: 0.1),
                    shape: BoxShape.circle,
                    border: Border.all(
                      color: const Color(0xFF4488FF).withValues(alpha: 0.3),
                      width: 2,
                    ),
                  ),
                  child: const Icon(
                    Icons.favorite_rounded,
                    color: Color(0xFF4488FF),
                    size: 40,
                  ),
                ),
              ),
              const SizedBox(height: 32),
              const Text(
                'Elige tu Rol',
                style: TextStyle(
                  fontSize: 28,
                  fontWeight: FontWeight.w800,
                  color: Colors.white,
                  letterSpacing: -1.0,
                ),
                textAlign: TextAlign.center,
              ),
              const SizedBox(height: 12),
              const Text(
                'Para sincronizar las ubicaciones y los toques hápticos, cada miembro de la pareja debe tener un rol diferente.',
                style: TextStyle(
                  fontSize: 14,
                  color: Color(0xFF8888A0),
                  height: 1.5,
                ),
                textAlign: TextAlign.center,
              ),
              const Spacer(),
              
              // Tarjeta Usuario A
              _buildRoleCard(
                role: 'A',
                title: 'Usuario A (Rol A)',
                subtitle: 'Ambos miembros tienen exactamente las mismas funciones. Configura un móvil como Rol A y el otro como Rol B para sincronizarse.',
                icon: Icons.looks_one_rounded,
                color: const Color(0xFF4488FF),
              ),
              const SizedBox(height: 16),
              
              // Tarjeta Usuario B
              _buildRoleCard(
                role: 'B',
                title: 'Usuario B (Rol B)',
                subtitle: 'Ambos miembros tienen exactamente las mismas funciones. Configura un móvil como Rol A y el otro como Rol B para sincronizarse.',
                icon: Icons.looks_two_rounded,
                color: const Color(0xFF00CC88),
              ),
              
              const Spacer(),
              
              // Botón Confirmar
              ElevatedButton(
                onPressed: _tempSelectedRole == null
                    ? null
                    : () async {
                        final prefs = await SharedPreferences.getInstance();
                        await prefs.setString('user_role', _tempSelectedRole!);
                        if (!mounted) return;
                        setState(() {
                          _showUserRoleSelection = false;
                          _initialized = false;
                        });
                        _bootstrap();
                      },
                style: ElevatedButton.styleFrom(
                  backgroundColor: const Color(0xFF4488FF),
                  foregroundColor: Colors.white,
                  disabledBackgroundColor: Colors.white.withValues(alpha: 0.05),
                  disabledForegroundColor: Colors.white.withValues(alpha: 0.25),
                  padding: const EdgeInsets.symmetric(vertical: 16),
                  shape: RoundedRectangleBorder(
                    borderRadius: BorderRadius.circular(14),
                  ),
                  elevation: 0,
                ),
                child: const Text(
                  'Confirmar y Continuar',
                  style: TextStyle(
                    fontSize: 16,
                    fontWeight: FontWeight.w600,
                  ),
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }

  Widget _buildRoleCard({
    required String role,
    required String title,
    required String subtitle,
    required IconData icon,
    required Color color,
  }) {
    final isSelected = _tempSelectedRole == role;
    return GestureDetector(
      onTap: () {
        setState(() {
          _tempSelectedRole = role;
        });
      },
      child: AnimatedContainer(
        duration: const Duration(milliseconds: 250),
        curve: Curves.easeOutCubic,
        padding: const EdgeInsets.all(20),
        decoration: BoxDecoration(
          color: isSelected
              ? color.withValues(alpha: 0.08)
              : Colors.white.withValues(alpha: 0.03),
          borderRadius: BorderRadius.circular(16),
          border: Border.all(
            color: isSelected
                ? color
                : Colors.white.withValues(alpha: 0.08),
            width: isSelected ? 2 : 1,
          ),
          boxShadow: isSelected
              ? [
                  BoxShadow(
                    color: color.withValues(alpha: 0.15),
                    blurRadius: 16,
                    spreadRadius: 1,
                  )
                ]
              : null,
        ),
        child: Row(
          children: [
            Container(
              width: 48,
              height: 48,
              decoration: BoxDecoration(
                color: isSelected
                    ? color.withValues(alpha: 0.15)
                    : Colors.white.withValues(alpha: 0.05),
                shape: BoxShape.circle,
              ),
              child: Icon(
                icon,
                color: isSelected ? color : Colors.white60,
                size: 24,
              ),
            ),
            const SizedBox(width: 16),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    title,
                    style: TextStyle(
                      fontSize: 16,
                      fontWeight: FontWeight.bold,
                      color: isSelected ? color : Colors.white,
                    ),
                  ),
                  const SizedBox(height: 4),
                  Text(
                    subtitle,
                    style: TextStyle(
                      fontSize: 12,
                      color: Colors.white.withValues(alpha: 0.5),
                      height: 1.4,
                    ),
                  ),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }
}
