import re

file_path = 'c:\\Users\\ovijo\\OneDrive\\Desktop\\Jessi\\app\\lib\\services\\foreground_service.dart'
with open(file_path, 'r', encoding='utf-8') as f:
    content = f.read()

# Add setBackgroundSendPort and sendCommand
service_methods = """  static SendPort? _backgroundSendPort;

  static void setBackgroundSendPort(SendPort port) {
    _backgroundSendPort = port;
  }

  static void sendCommand(String data) {
    _backgroundSendPort?.send(data);
  }

  /// Actualizar el texto"""
content = content.replace("  /// Actualizar el texto", service_methods)

# Replace _ForegroundTaskHandler completely
handler_impl = """class _ForegroundTaskHandler extends TaskHandler {
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
"""

content = re.sub(r'class _ForegroundTaskHandler extends TaskHandler \{.*$', handler_impl, content, flags=re.DOTALL)

with open(file_path, 'w', encoding='utf-8') as f:
    f.write(content)

print("ForegroundService replacement complete")
