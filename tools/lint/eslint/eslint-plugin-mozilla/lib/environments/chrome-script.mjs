/**
 * @file Defines the environment for SpecialPowers chrome script.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

import specialPowersSandboxGlobals from "./special-powers-sandbox.mjs";

export default {
  globals: {
    // testing/specialpowers/content/SpecialPowersParent.sys.mjs
    // SPLoadChromeScript block
    createWindowlessBrowser: "readonly",
    sendAsyncMessage: "readonly",
    addMessageListener: "readonly",
    removeMessageListener: "readonly",
    actorParent: "readonly",

    ...specialPowersSandboxGlobals.globals,
  },
};
