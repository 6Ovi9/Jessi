import re

with open('app/lib/main.dart', 'r', encoding='utf-8') as f:
    content = f.read()

# 1. Remove initCommunicationPort
content = re.sub(r'\s*FlutterForegroundTask\.initCommunicationPort\(\);', '', content)

# 2. Fix try-catch block
try_block_old = """      try {
        if (message is String) {
          final payload = jsonDecode(message) as Map<String, dynamic>;
          final type = payload['type'];
          final data = payload['payload'];
          
          if (type == 'token_update' || type == 'session_expired') {
            final syncService = context.read<SyncService>();
            if (type == 'token_update') {
              final accessToken = data['access_token'];
              final refreshToken = data['refresh_token'];
              // Se puede setear la sesión acá o el SyncService lo hace
              syncService.resolveAuthWait(true, refreshToken);
            } else {
              syncService.resolveAuthWait(false, null);
            }
          } else {
            if (mounted) {
              final bleService = context.read<BleService>();
              bleService.processBackgroundMessage(message);
            }
          }
        } catch (e) {
          debugPrint('[APP] Error routing background message: $e');
        }
      }"""

try_block_new = """      if (message is String) {
        try {
          final payload = jsonDecode(message) as Map<String, dynamic>;
          final type = payload['type'];
          final data = payload['payload'];
          
          if (type == 'token_update' || type == 'session_expired') {
            final syncService = context.read<SyncService>();
            if (type == 'token_update') {
              final refreshToken = data['refresh_token'];
              syncService.resolveAuthWait(true, refreshToken);
            } else {
              syncService.resolveAuthWait(false, null);
            }
          } else {
            if (mounted) {
              final bleService = context.read<BleService>();
              bleService.processBackgroundMessage(message);
            }
          }
        } catch (e) {
          debugPrint('[APP] Error routing background message: $e');
        }
      }"""

content = content.replace(try_block_old, try_block_new)

# 3. Fix checkAndRequestPermissions for BleService
perms_old = """      final hasLocPerms = await locationService.checkAndRequestPermissions();
      final hasBlePerms = await bleService.checkAndRequestPermissions();
      
      if (!hasLocPerms || !hasBlePerms) {
        throw Exception('Faltan permisos requeridos de GPS o BLE.');
      }"""

perms_new = """      final hasLocPerms = await locationService.checkAndRequestPermissions();
      
      if (!hasLocPerms) {
        throw Exception('Faltan permisos requeridos de GPS.');
      }"""
content = content.replace(perms_old, perms_new)

# 4. Fix FlutterForegroundTask.sendDataToTask
send_old = """      FlutterForegroundTask.sendDataToTask(jsonEncode({
        "type": "request_full_state",
        "payload": {}
      }));"""

send_new = """      fg.ForegroundService.sendCommand(jsonEncode({
        "type": "request_full_state",
        "payload": {}
      }));"""
content = content.replace(send_old, send_new)

# 5. Remove 'dart:convert' from foreground_service.dart
with open('app/lib/services/foreground_service.dart', 'r', encoding='utf-8') as fs:
    fs_content = fs.read()
fs_content = fs_content.replace("import 'dart:convert';\n", '')
with open('app/lib/services/foreground_service.dart', 'w', encoding='utf-8') as fs:
    fs.write(fs_content)

# 6. Remove unused imports in ble_service.dart
with open('app/lib/services/ble_service.dart', 'r', encoding='utf-8') as fs:
    fs_content = fs.read()
fs_content = fs_content.replace("import 'package:flutter_foreground_task/flutter_foreground_task.dart';\n", '')
with open('app/lib/services/ble_service.dart', 'w', encoding='utf-8') as fs:
    fs.write(fs_content)

# 7. Remove unused import in home_screen.dart
with open('app/lib/screens/home_screen.dart', 'r', encoding='utf-8') as fs:
    fs_content = fs.read()
fs_content = fs_content.replace("import 'dart:math';\n", '')
with open('app/lib/screens/home_screen.dart', 'w', encoding='utf-8') as fs:
    fs.write(fs_content)

with open('app/lib/main.dart', 'w', encoding='utf-8') as f:
    f.write(content)

print("Fixes applied")
