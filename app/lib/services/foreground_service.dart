import 'dart:async';
import 'dart:isolate';
import 'dart:ui';
import 'package:flutter/widgets.dart';
import 'package:flutter_foreground_task/flutter_foreground_task.dart';
import 'background_engine.dart';

/// Controlador del Android Foreground Service.
///
/// Mantiene la app ejecutándose en background indefinidamente mediante
/// una notificación persistente silenciosa. Esto es necesario para:
///
/// - Mantener la conexión BLE activa con el reloj
/// - Continuar el polling GPS en background
/// - Mantener la conexión WebSocket con Supabase Realtime
///
/// **Nota Android 14+**: El foregroundServiceType debe ser 'location' Y
/// 'connectedDevice' simultáneamente. Si solo se declara uno, Android
/// puede matar el proceso en background.
class ForegroundService {
  /// Inicializar la configuración del Foreground Service.
  ///
  /// Debe llamarse una vez en el inicio de la app, ANTES de iniciar el servicio.
  static void initialize() {
    FlutterForegroundTask.init(
      androidNotificationOptions: AndroidNotificationOptions(
        channelId: 'nexus_halo_foreground',
        channelName: 'Nexus Halo',
        channelDescription: 'Servicio de seguimiento de ubicación y conexión BLE',
        channelImportance: NotificationChannelImportance.LOW,
        priority: NotificationPriority.LOW,
      ),
      iosNotificationOptions: const IOSNotificationOptions(
        showNotification: false,
        playSound: false,
      ),
      foregroundTaskOptions: const ForegroundTaskOptions(
        interval: 5000,
        isOnceEvent: false,
        autoRunOnBoot: true,
        autoRunOnMyPackageReplaced: true,
        allowWakeLock: true,
        allowWifiLock: true,
      ),
    );

    print('[FG] Foreground service initialized');
  }

  /// Iniciar el Foreground Service.
  ///
  /// Muestra la notificación persistente y previene que Android
  /// destruya el proceso en Doze Mode.
  static Future<bool> start() async {
    try {
      if (await FlutterForegroundTask.isRunningService) {
        print('[FG] Service already running');
        return true;
      }

      final result = await FlutterForegroundTask.startService(
        notificationTitle: 'Nexus Halo activo',
        notificationText: 'Conectado y rastreando ubicación',
        callback: _startCallback,
      );

      print('[FG] Service started: $result');
      return result;
    } catch (e) {
      print('[FG] Service start failed: $e');
      return false;
    }
  }

  /// Detener el Foreground Service.
  static Future<bool> stop() async {
    try {
      final result = await FlutterForegroundTask.stopService();
      print('[FG] Service stopped: $result');
      return result;
    } catch (e) {
      print('[FG] Service stop failed: $e');
      return false;
    }
  }

  static SendPort? _backgroundSendPort;

  static void setBackgroundSendPort(SendPort port) {
    _backgroundSendPort = port;
  }

  static void sendCommand(String data) {
    _backgroundSendPort?.send(data);
  }

  /// Actualizar el texto de la notificación.
  ///
  /// Útil para mostrar el estado actual (modo de polling, distancia, etc.)
  static Future<void> updateNotification({
    String? title,
    String? text,
  }) async {
    try {
      await FlutterForegroundTask.updateService(
        notificationTitle: title ?? 'Nexus Halo activo',
        notificationText: text ?? 'Conectado y rastreando ubicación',
      );
    } catch (e) {
      print('[FG] Notification update failed: $e');
    }
  }

  /// ¿Está el servicio ejecutándose?
  static Future<bool> get isRunning async {
    return await FlutterForegroundTask.isRunningService;
  }
}

/// Callback que se ejecuta cuando el Foreground Service arranca.
///
/// Este es el punto de entrada del servicio en background.
@pragma('vm:entry-point')
void _startCallback() {
  FlutterForegroundTask.setTaskHandler(_ForegroundTaskHandler());
}

/// Handler del Foreground Service.
///
/// Gestiona el lifecycle del servicio (start, repeat, destroy) e inicializa
/// el BackgroundEngine.
class _ForegroundTaskHandler extends TaskHandler {
  BackgroundEngine? _engine;
  ReceivePort? _receivePort;

  @override
  Future<void> onStart(DateTime timestamp, SendPort? sendPort) async {
    WidgetsFlutterBinding.ensureInitialized();
    DartPluginRegistrant.ensureInitialized();
    print('[FG] Background Isolate started at $timestamp');

    _receivePort = ReceivePort();
    sendPort?.send(_receivePort!.sendPort);

    _engine = BackgroundEngine(sendPort: sendPort);

    _receivePort?.listen((data) {
      if (data is String) {
        _engine?.onReceiveData(data);
      }
    });

    try {
      await _engine?.start();
    } catch (e) {
      print('[FG] Engine start failed: $e');
    }
  }

  @override
  void onRepeatEvent(DateTime timestamp, SendPort? sendPort) {
    // onTick not used anymore
  }

  @override
  Future<void> onDestroy(DateTime timestamp, SendPort? sendPort) async {
    print('[FG] Task handler destroyed at $timestamp');
    _receivePort?.close();
    _engine = null;
  }

  @override
  void onNotificationButtonPressed(String id) {
    print('[FG] Notification button pressed: $id');
  }

  @override
  void onNotificationPressed() {
    print('[FG] Notification pressed — opening app');
    FlutterForegroundTask.launchApp();
  }
}
