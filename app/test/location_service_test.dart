import 'package:flutter_test/flutter_test.dart';
import 'package:geolocator/geolocator.dart';
import 'package:nexus_halo/services/location_service.dart';

void main() {
  group('LocationService permission helper', () {
    test('upgrades Android while-in-use permission once', () {
      expect(
        LocationService.shouldUpgradeToAlwaysPermission(
          isAndroid: true,
          permission: LocationPermission.whileInUse,
        ),
        isTrue,
      );
    });

    test('does not upgrade non-Android permissions', () {
      expect(
        LocationService.shouldUpgradeToAlwaysPermission(
          isAndroid: false,
          permission: LocationPermission.whileInUse,
        ),
        isFalse,
      );
    });
  });
}
