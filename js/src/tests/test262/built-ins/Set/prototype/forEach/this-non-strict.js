// Copyright (C) 2015 the V8 project authors. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
esid: sec-set.prototype.foreach
description: >
    Set.prototype.forEach ( callbackfn [ , thisArg ] )

    ...
    5. If thisArg was supplied, let T be thisArg; else let T be undefined.
    ...

flags: [noStrict]
---*/

var s = new Set([1]);
var counter = 0;
var globalObject = this;

s.forEach(function() {
  assert.sameValue(this, globalObject, "`this` is the global object in non-strict mode code");
  counter++;
});

assert.sameValue(counter, 1, "`forEach` is not a no-op");

reportCompare(0, 0);
