import 'package:flutter_test/flutter_test.dart';
import 'package:nexus_halo/models/config_model.dart';

void main() {
  group('WatchConfig', () {
    test('normalizes legacy haptic pattern values to both', () {
      final config = WatchConfig.fromJson({
        'user_id': 'A',
        'haptic_pattern': 'default',
      });

      expect(config.hapticPattern, 'both');
    });

    test('keeps partner haptic pattern as partner', () {
      final config = WatchConfig.fromJson({
        'user_id': 'A',
        'haptic_pattern': 'partner',
      });

      expect(config.hapticPattern, 'partner');
    });
  });
}
