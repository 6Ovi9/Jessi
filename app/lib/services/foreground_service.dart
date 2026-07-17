import 'dart:isolate';
import 'dart:ui';
import 'package:flutter/widgets.dart';

import 'package:flutter_foreground_task/flutter_foreground_task.dart';
import 'ble_service.dart';
import 'location_service.dart';

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
        // Icono de la notificación (debe existir en res/drawable)
        // iconData: const NotificationIconData(
        //   resType: ResourceType.drawable,
        //   resPrefix: ResourcePrefix.ic,
        //   name: 'notification_silent',
        // ),
      ),
      iosNotificationOptions: const IOSNotificationOptions(
        showNotification: false,
        playSound: false,
      ),
      foregroundTaskOptions: const ForegroundTaskOptions(
        interval: 5000, // Se ejecuta cada 5 segundos (mínimo para el timer interno)
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
      return true;
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
      return true;
    } catch (e) {
      print('[FG] Service stop failed: $e');
      return false;
    }
  }

  /// Actualizar el texto de la notificación.
  ///
  /// Útil para mostrar el estado actual (modo de polling, distancia, etc.)
  static Future<void> updateNotification({
    String? title,
    String? text,
  }) async {
    await FlutterForegroundTask.updateService(
      notificationTitle: title ?? 'Nexus Halo activo',
      notificationText: text ?? 'Conectado y rastreando ubicación',
    );
  }

  /// ¿Está el servicio ejecutándose?
  static Future<bool> get isRunning async {
    return await FlutterForegroundTask.isRunningService;
  }
}

/// Callback que se ejecuta cuando el Foreground Service arranca.
///
/// Este es el punto de entrada del servicio en background.
/// Nota: En flutter_foreground_task, el "trabajo" real se hace en el
/// main isolate (la app Flutter), no aquí. Este callback es solo
/// para el lifecycle del servicio Android.
@pragma('vm:entry-point')
void _startCallback() {
  FlutterForegroundTask.setTaskHandler(_ForegroundTaskHandler());
}

/// Handler del Foreground Service.
///
/// Gestiona el lifecycle del servicio (start, repeat, destroy).
/// El trabajo real de BLE/GPS/Sync se hace en los services de la app,
/// no aquí. Este handler solo mantiene el servicio vivo.
class _ForegroundTaskHandler extends TaskHandler {

  @override
  void onStart(DateTime timestamp, SendPort? sendPort) {
    WidgetsFlutterBinding.ensureInitialized();
    DartPluginRegistrant.ensureInitialized();
    print('[FG] Background Isolate started at $timestamp');
  }

  @override
  void onRepeatEvent(DateTime timestamp, SendPort? sendPort) {
    // Este evento se dispara cada `interval` ms (5000ms = 5s) en un isolate secundario.
    // Enviamos un mensaje al isolate principal (UI) para forzar que Android lo
    // despierte (wake up) temporalmente y procese el Event Loop, permitiendo
    // que los timers de LocationService y BleService sigan funcionando en background.
    sendPort?.send('tick');
  }

  @override
  void onDestroy(DateTime timestamp, SendPort? sendPort) {
    print('[FG] Task handler destroyed at $timestamp');
  }

  @override
  void onNotificationButtonPressed(String id) {
    // Si añadimos botones a la notificación en el futuro
    print('[FG] Notification button pressed: $id');
  }

  @override
  void onNotificationPressed() {
    // El usuario tocó la notificación — abrir la app
    print('[FG] Notification pressed — opening app');
    FlutterForegroundTask.launchApp();
  }

}
