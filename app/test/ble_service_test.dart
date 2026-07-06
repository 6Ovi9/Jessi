import 'package:flutter_test/flutter_test.dart';
import 'package:nexus_halo/services/ble_service.dart';
import 'package:permission_handler/permission_handler.dart';

void main() {
  group('BleService permission selection', () {
    test('uses granular Android 12+ BLE permissions', () {
      final permissions = BleService.requiredPermissionsForScan(
        isAndroid: true,
        androidSdkInt: 31,
      );

      expect(
        permissions,
        containsAll(<Permission>[
          Permission.bluetoothScan,
          Permission.bluetoothConnect,
          Permission.locationWhenInUse,
        ]),
      );
    });

    test('uses legacy BLE permissions on older Android versions', () {
      final permissions = BleService.requiredPermissionsForScan(
        isAndroid: true,
        androidSdkInt: 30,
      );

      expect(
        permissions,
        containsAll(<Permission>[
          Permission.bluetooth,
          Permission.locationWhenInUse,
        ]),
      );
    });
  });
}
