// META: script=/resources/testdriver.js?feature=bidi
// META: script=/resources/testdriver-vendor.js
// META: script=/bluetooth/resources/bluetooth-test.js
// META: script=/bluetooth/resources/bluetooth-fake-devices.js
// META: timeout=long
'use strict';
const test_desc = 'Simple filter selects matching device.';

bluetooth_bidi_test(
    () => setUpHealthThermometerAndHeartRateDevices()
              .then(
                  () => requestDeviceWithTrustedClick(
                      {filters: [{services: ['health_thermometer']}]}))
              .then(device => assert_equals(device.name, 'Health Thermometer')),
    test_desc);
