import 'package:flutter_test/flutter_test.dart';
import 'package:nexus_halo/services/ble_service.dart';

void main() {
  group('BleService reconnect policy', () {
    test('schedules reconnect while retry budget remains', () {
      expect(
        BleService.shouldScheduleReconnect(
          isConnected: false,
          currentAttempt: 1,
          maxAttempts: 3,
        ),
        isTrue,
      );
    });

    test('stops retrying once the retry budget is exhausted', () {
      expect(
        BleService.shouldScheduleReconnect(
          isConnected: false,
          currentAttempt: 3,
          maxAttempts: 3,
        ),
        isFalse,
      );
    });
  });
}
