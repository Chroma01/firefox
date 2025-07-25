/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { ctypes } = ChromeUtils.importESModule(
  "resource://gre/modules/ctypes.sys.mjs"
);
const { setTimeout } = ChromeUtils.importESModule(
  "resource://gre/modules/Timer.sys.mjs"
);
const { TestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/TestUtils.sys.mjs"
);

const kDllName = "modules-test.dll";

let gCurrentPidStr;

async function load_and_free(name) {
  // Dynamically load a DLL which we have hard-coded as untrusted; this should
  // appear in the payload.
  let dllHandle = ctypes.open(do_get_file(name).path);
  if (dllHandle) {
    dllHandle.close();
    dllHandle = null;
  }
  // Give the thread some cycles to process a loading event.
  // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
  await new Promise(resolve => setTimeout(resolve, 50));
}

let gFirstPing;
add_setup(async function () {
  do_get_profile();

  Services.fog.initializeFOG();

  // To avoid reformatting this test too much, we're going to accept
  // that the first "third-party-modules" ping is submitted during setup.
  await GleanPings.thirdPartyModules.testBeforeNextSubmit(() => {
    gFirstPing = {
      blockedModules: Glean.thirdPartyModules.blockedModules.testGetValue(),
      modules: Glean.thirdPartyModules.modules.testGetValue(),
      processes: Glean.thirdPartyModules.processes.testGetValue(),
    };
  });

  // Dynamically load a DLL which we have hard-coded as untrusted; this should
  // appear in the payload.
  await load_and_free(kDllName);

  // Force the timer to fire (using a small interval).
  Cc["@mozilla.org/updates/timer-manager;1"]
    .getService(Ci.nsIObserver)
    .observe(null, "utm-test-init", "");
  Services.prefs.setIntPref(
    "toolkit.telemetry.untrustedModulesPing.frequency",
    0
  );
  Services.prefs.setStringPref("app.update.url", "http://localhost");

  let currentPid = Services.appinfo.processID;
  gCurrentPidStr = "browser.0x" + currentPid.toString(16);

  // Start the local ping server and setup Telemetry to use it during the tests.
  PingServer.start();
  Services.prefs.setStringPref(
    TelemetryUtils.Preferences.Server,
    "http://localhost:" + PingServer.port
  );

  return TelemetryController.testSetup();
});

registerCleanupFunction(function () {
  return PingServer.stop();
});

// This tests basic end-to-end functionality of the untrusted modules
// telemetry ping. We force the ping to fire, capture the result, and test for:
// - Basic payload structure validity.
// - Expected results for a few specific DLLs
add_task(
  {
    skip_if: () =>
      Services.prefs.getBoolPref("telemetry.fog.artifact_build", false),
  },
  async function test_send_ping() {
    let expectedModules = [
      // This checks that a DLL loaded during runtime is evaluated properly.
      // This is hard-coded as untrusted in toolkit/xre/UntrustedModules.cpp for
      // testing purposes.
      {
        nameMatch: new RegExp(kDllName, "i"),
        expectedTrusted: false,
        wasFound: false,
      },
      {
        nameMatch: /kernelbase.dll/i,
        expectedTrusted: true,
        wasFound: false,
      },
    ];

    await TestUtils.waitForCondition(
      () => !!gFirstPing,
      "Waiting for Glean ping"
    );

    // There is a tiny chance some other ping is being sent legitimately before
    // the one we care about. Spin until we find the correct ping type.
    let found;
    await TestUtils.waitForCondition(async () => {
      found = await PingServer.promiseNextPing();
      return found.type == "third-party-modules";
    }, "Waiting for Telemetry ping");

    // Test the ping payload's validity.
    Assert.ok(found, "Untrusted modules ping submitted");
    Assert.ok(found.environment, "Ping has an environment");
    Assert.notEqual(typeof found.clientId, "undefined", "Ping has a client ID");

    Assert.equal(found.payload.structVersion, 1, "Version is correct");
    Assert.ok(found.payload.modules, "'modules' object exists");
    Assert.ok(gFirstPing.modules, "`modules` set");
    Assert.ok(Array.isArray(found.payload.modules), "'modules' is an array");
    Assert.ok(Array.isArray(gFirstPing.modules), "`modules` is an array");
    Assert.ok(found.payload.blockedModules, "'blockedModules' object exists");
    Assert.ok(gFirstPing.blockedModules, "`blockedModules` exists");
    Assert.ok(
      Array.isArray(found.payload.blockedModules),
      "'blockedModules' is an array"
    );
    Assert.ok(
      Array.isArray(gFirstPing.blockedModules),
      "`blockedModules` is an array"
    );
    // Unfortunately, the way this test is run it doesn't usually get a launcher
    // process, so the blockedModules member doesn't get populated. This is the
    // same structure that's used in the about:third-party page, though, so we
    // have coverage in browser_aboutthirdparty.js that this is correct.
    Assert.ok(found.payload.processes, "'processes' object exists");
    Assert.ok(gFirstPing.processes, "`processes` set");
    Assert.ok(
      gCurrentPidStr in found.payload.processes,
      `Current process "${gCurrentPidStr}" is included in payload`
    );
    const curProcInfos = gFirstPing.processes.filter(
      process => process.processName == gCurrentPidStr
    );
    Assert.equal(
      curProcInfos.length,
      1,
      `Current process "${gCurrentPidStr}" is included in payload`
    );
    const curProcInfo = curProcInfos[0];

    let ourProcInfo = found.payload.processes[gCurrentPidStr];
    Assert.equal(
      ourProcInfo.processType,
      "browser",
      "'processType' is correct"
    );
    Assert.equal(
      curProcInfo.processType,
      "browser",
      "'processType' is correct"
    );
    Assert.equal(typeof ourProcInfo.elapsed, "number", "'elapsed' exists");
    Assert.ok(
      Number.isFinite(Number.parseFloat(curProcInfo.elapsed)),
      "'elapsed' is a number (in a string)"
    );
    Assert.equal(
      ourProcInfo.sanitizationFailures,
      0,
      "'sanitizationFailures' is 0"
    );
    Assert.equal(
      curProcInfo.sanitizationFailures,
      0,
      "'sanitizationFailures' is 0"
    );
    Assert.equal(ourProcInfo.trustTestFailures, 0, "'trustTestFailures' is 0");
    Assert.equal(curProcInfo.trustTestFailures, 0, "'trustTestFailures' is 0");

    Assert.equal(
      ourProcInfo.combinedStacks.stacks.length,
      ourProcInfo.events.length,
      "combinedStacks.stacks.length == events.length"
    );
    Assert.equal(
      curProcInfo.combinedStacks.stacks.length,
      curProcInfo.events.length,
      "combinedStacks.stacks.length == events.length"
    );

    for (let event of ourProcInfo.events) {
      Assert.equal(
        typeof event.processUptimeMS,
        "number",
        "'processUptimeMS' exists"
      );
      Assert.equal(typeof event.threadID, "number", "'threadID' exists");
      Assert.equal(typeof event.baseAddress, "string", "'baseAddress' exists");

      Assert.equal(typeof event.moduleIndex, "number", "'moduleIndex' exists");
      Assert.greaterOrEqual(
        event.moduleIndex,
        0,
        "'moduleIndex' is non-negative"
      );

      Assert.equal(typeof event.isDependent, "boolean", "'isDependent' exists");
      Assert.ok(!event.isDependent, "'isDependent' is false");

      Assert.equal(typeof event.loadStatus, "number", "'loadStatus' exists");
      Assert.equal(event.loadStatus, 0, "'loadStatus' is 0 (Loaded)");

      let modRecord = found.payload.modules[event.moduleIndex];
      Assert.ok(modRecord, "module record for this event exists");
      Assert.equal(
        typeof modRecord.resolvedDllName,
        "string",
        "'resolvedDllName' exists"
      );
      Assert.equal(
        typeof modRecord.trustFlags,
        "number",
        "'trustFlags' exists"
      );

      let mod = expectedModules.find(function (elem) {
        return elem.nameMatch.test(modRecord.resolvedDllName);
      });

      if (mod) {
        mod.wasFound = true;
      }
    }

    for (let event of curProcInfo.events) {
      Assert.ok(
        Number.isFinite(Number.parseFloat(event.processUptimeMS)),
        "'elapsed' is a number (in a string)"
      );
      Assert.greater(event.processUptimeMS, 0);
      Assert.greater(event.threadID, -1);
      Assert.ok(event.baseAddress);
      Assert.greater(event.moduleIndex, -1);
      Assert.ok(!event.isDependent);
      Assert.equal(event.loadStatus, 0, "(Loaded)");

      const module = gFirstPing.modules[event.moduleIndex];
      Assert.ok(module, "module record for this event exists");
      Assert.ok(module.resolvedDllName, "module has a name");
      Assert.ok(Number.isFinite(module.trustFlags), "trustFlags are present");

      const expectedMod = expectedModules.find(em =>
        em.nameMatch.test(module.resolvedDllName)
      );
      if (expectedMod) {
        expectedMod.wasFoundGlean = true;
      }
    }

    for (let x of expectedModules) {
      Assert.equal(
        !x.wasFound,
        x.expectedTrusted,
        `Trustworthiness == expected for module: ${x.nameMatch.source}`
      );
      Assert.equal(
        !x.wasFoundGlean,
        x.expectedTrusted,
        `Trustworthiness == expected for module: ${x.nameMatch.source}`
      );
    }
  }
);

// This tests the flags INCLUDE_OLD_LOADEVENTS and KEEP_LOADEVENTS_NEW
// controls the method's return value and the internal storages
// "Staging" and "Settled" correctly.
add_task(async function test_new_old_instances() {
  const kIncludeOld = Telemetry.INCLUDE_OLD_LOADEVENTS;
  const kKeepNew = Telemetry.KEEP_LOADEVENTS_NEW;
  const get_events_count = data => data.processes[gCurrentPidStr].events.length;

  // Make sure |baseline| has at least one instance.
  await load_and_free(kDllName);

  // Make sure all instances are "old"
  const baseline = await Telemetry.getUntrustedModuleLoadEvents(kIncludeOld);
  const baseline_count = get_events_count(baseline);
  print("baseline_count = " + baseline_count);
  print("baseline = " + JSON.stringify(baseline));

  await Assert.rejects(
    Telemetry.getUntrustedModuleLoadEvents(),
    e => e.result == Cr.NS_ERROR_NOT_AVAILABLE,
    "New instances should not exist!"
  );

  await load_and_free(kDllName); // A

  // Passing kIncludeOld and kKeepNew is unsupported.  A is kept new.
  await Assert.rejects(
    Telemetry.getUntrustedModuleLoadEvents(kIncludeOld | kKeepNew),
    e => e.result == Cr.NS_ERROR_INVALID_ARG,
    "Passing unsupported flag combination should throw an exception!"
  );

  await load_and_free(kDllName); // B

  // After newly loading B, the new instances we have is {A, B}
  // Both A and B are still kept new.
  let payload = await Telemetry.getUntrustedModuleLoadEvents(kKeepNew);
  print("payload = " + JSON.stringify(payload));
  Assert.equal(get_events_count(payload), 2);

  await load_and_free(kDllName); // C

  // After newly loading C, the new instances we have is {A, B, C}
  // All of A, B, and C are now marked as old.
  payload = await Telemetry.getUntrustedModuleLoadEvents();
  Assert.equal(get_events_count(payload), 3);

  payload = await Telemetry.getUntrustedModuleLoadEvents(kIncludeOld);
  // payload is {baseline, A, B, C}
  Assert.equal(get_events_count(payload), baseline_count + 3);
});

// This tests the flag INCLUDE_PRIVATE_FIELDS_IN_LOADEVENTS returns
// data including private fields.
add_task(async function test_private_fields() {
  await load_and_free(kDllName);
  const data = await Telemetry.getUntrustedModuleLoadEvents(
    Telemetry.KEEP_LOADEVENTS_NEW |
      Telemetry.INCLUDE_PRIVATE_FIELDS_IN_LOADEVENTS
  );

  for (const module of data.modules) {
    Assert.ok(!("resolvedDllName" in module));
    Assert.ok("dllFile" in module);
    Assert.ok(module.dllFile.QueryInterface);
    Assert.ok(module.dllFile.QueryInterface(Ci.nsIFile));
  }
});

// This tests the flag EXCLUDE_STACKINFO_FROM_LOADEVENTS correctly
// merges "Staging" and "Settled" on a JS object correctly, and
// the "combinedStacks" field is really excluded.
add_task(async function test_exclude_stack() {
  const baseline = await Telemetry.getUntrustedModuleLoadEvents(
    Telemetry.EXCLUDE_STACKINFO_FROM_LOADEVENTS |
      Telemetry.INCLUDE_OLD_LOADEVENTS
  );
  Assert.ok(!("combinedStacks" in baseline.processes[gCurrentPidStr]));
  const baseSet = baseline.processes[gCurrentPidStr].events.map(
    x => x.processUptimeMS
  );

  await load_and_free(kDllName);
  await load_and_free(kDllName);
  const newLoadsWithStack = await Telemetry.getUntrustedModuleLoadEvents(
    Telemetry.KEEP_LOADEVENTS_NEW
  );
  Assert.ok("combinedStacks" in newLoadsWithStack.processes[gCurrentPidStr]);
  const newSet = newLoadsWithStack.processes[gCurrentPidStr].events.map(
    x => x.processUptimeMS
  );

  const merged = baseSet.concat(newSet);

  const allData = await Telemetry.getUntrustedModuleLoadEvents(
    Telemetry.KEEP_LOADEVENTS_NEW |
      Telemetry.EXCLUDE_STACKINFO_FROM_LOADEVENTS |
      Telemetry.INCLUDE_OLD_LOADEVENTS
  );
  Assert.ok(!("combinedStacks" in allData.processes[gCurrentPidStr]));
  const allSet = allData.processes[gCurrentPidStr].events.map(
    x => x.processUptimeMS
  );

  Assert.deepEqual(allSet.sort(), merged.sort());
});
