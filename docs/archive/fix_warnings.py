import re

# Fix home_screen.dart
with open('c:\\Users\\ovijo\\OneDrive\\Desktop\\Jessi\\app\\lib\\screens\\home_screen.dart', 'r', encoding='utf-8') as f:
    content = f.read()

content = content.replace("                    await loc.forceUpdate();\n", "")

with open('c:\\Users\\ovijo\\OneDrive\\Desktop\\Jessi\\app\\lib\\screens\\home_screen.dart', 'w', encoding='utf-8') as f:
    f.write(content)

# Fix unused declarations in ble_service.dart
with open('c:\\Users\\ovijo\\OneDrive\\Desktop\\Jessi\\app\\lib\\services\\ble_service.dart', 'r', encoding='utf-8') as f:
    content = f.read()

# I will just remove the entire methods _scheduleReconnect, _onConnected, _onDisconnected. They are not needed in UI Proxy anyway.
content = re.sub(r'\s*void _scheduleReconnect\(\) \{.*?(?=\n  @visibleForTesting)', '', content, flags=re.DOTALL)
content = re.sub(r'\s*Future<void> _onConnected\(String deviceId\) async \{.*?(?=\n  /// Llamado al perder la conexión)', '', content, flags=re.DOTALL)
content = re.sub(r'\s*void _onDisconnected\(\) \{.*?(?=\n  // ── Subscripciones)', '', content, flags=re.DOTALL)

with open('c:\\Users\\ovijo\\OneDrive\\Desktop\\Jessi\\app\\lib\\services\\ble_service.dart', 'w', encoding='utf-8') as f:
    f.write(content)

print("Warnings fixed")
