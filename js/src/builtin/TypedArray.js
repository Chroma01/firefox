/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "TypedArrayConstants.h"

function ViewedArrayBufferIfReified(tarray) {
  assert(IsTypedArray(tarray), "non-typed array asked for its buffer");

  var buf = UnsafeGetReservedSlot(tarray, JS_TYPEDARRAYLAYOUT_BUFFER_SLOT);
  assert(
    buf === false ||
    buf === true ||
    (IsObject(buf) &&
      (GuardToArrayBuffer(buf) !== null ||
        GuardToSharedArrayBuffer(buf) !== null)),
    "unexpected value in buffer slot"
  );
  return IsObject(buf) ? buf : null;
}

function GetArrayBufferFlagsOrZero(buffer) {
  // A typed array with a null buffer has never had its buffer exposed. Return
  // the default flags, which is zero.
  if (buffer === null) {
    return 0;
  }

  assert(
    GuardToArrayBuffer(buffer) !== null ||
    GuardToSharedArrayBuffer(buffer) !== null,
    "non-ArrayBuffer passed to IsDetachedBuffer"
  );

  // Return zero for shared array buffers.
  if ((buffer = GuardToArrayBuffer(buffer)) === null) {
    return 0;
  }

  return UnsafeGetInt32FromReservedSlot(buffer, JS_ARRAYBUFFER_FLAGS_SLOT);
}

function EnsureAttachedArrayBuffer(tarray) {
  var buffer = ViewedArrayBufferIfReified(tarray);
  var flags = GetArrayBufferFlagsOrZero(buffer);
  if ((flags & JS_ARRAYBUFFER_DETACHED_FLAG) !== 0) {
    ThrowTypeError(JSMSG_TYPED_ARRAY_DETACHED);
  }
}

function EnsureAttachedMutableArrayBuffer(tarray) {
  var buffer = ViewedArrayBufferIfReified(tarray);
  var flags = GetArrayBufferFlagsOrZero(buffer);
  if ((flags & JS_ARRAYBUFFER_DETACHED_FLAG) !== 0) {
    ThrowTypeError(JSMSG_TYPED_ARRAY_DETACHED);
  }

  // Additional step from Immutable ArrayBuffer proposal.
  if ((flags & JS_ARRAYBUFFER_IMMUTABLE_FLAG) !== 0) {
    ThrowTypeError(JSMSG_ARRAYBUFFER_IMMUTABLE);
  }
}

function EnsureAttachedArrayBufferMethod() {
  EnsureAttachedArrayBuffer(this);
}

// A function which ensures that the argument is either a typed array or a
// cross-compartment wrapper for a typed array and that the typed array involved
// has an attached array buffer.  If one of those conditions doesn't hold (wrong
// kind of argument, or detached array buffer), an exception is thrown.
function EnsureTypedArrayWithArrayBuffer(arg) {
  if (IsObject(arg) && IsTypedArray(arg)) {
    EnsureAttachedArrayBuffer(arg);
    return;
  }

  callFunction(
    CallTypedArrayMethodIfWrapped,
    arg,
    "EnsureAttachedArrayBufferMethod"
  );
}

// ES2019 draft rev 85ce767c86a1a8ed719fe97e978028bff819d1f2
// 7.3.20 SpeciesConstructor ( O, defaultConstructor )
//
// SpeciesConstructor function optimized for TypedArrays to avoid calling
// ConstructorForTypedArray, a non-inlineable runtime function, in the normal
// case.
function TypedArraySpeciesConstructor(obj) {
  // Step 1.
  assert(IsObject(obj), "not passed an object");

  // Step 2.
  var ctor = obj.constructor;

  // Step 3.
  if (ctor === undefined) {
    return ConstructorForTypedArray(obj);
  }

  // Step 4.
  if (!IsObject(ctor)) {
    ThrowTypeError(JSMSG_OBJECT_REQUIRED, "object's 'constructor' property");
  }

  // Steps 5.
  var s = ctor[GetBuiltinSymbol("species")];

  // Step 6.
  if (IsNullOrUndefined(s)) {
    return ConstructorForTypedArray(obj);
  }

  // Step 7.
  if (IsConstructor(s)) {
    return s;
  }

  // Step 8.
  ThrowTypeError(
    JSMSG_NOT_CONSTRUCTOR,
    "@@species property of object's constructor"
  );
}

// ES2017 draft rev 6859bb9ccaea9c6ede81d71e5320e3833b92cb3e
// 22.2.3.5.1 Runtime Semantics: ValidateTypedArray ( O )
function ValidateWritableTypedArray(obj) {
  if (IsObject(obj)) {
    /* Steps 3-5 (non-wrapped typed arrays). */
    if (IsTypedArray(obj)) {
      // EnsureAttachedMutableArrayBuffer throws for detached or immutable
      // array buffers.
      EnsureAttachedMutableArrayBuffer(obj);
      return;
    }

    /* Steps 3-5 (wrapped typed arrays). */
    if (IsPossiblyWrappedTypedArray(obj)) {
      if (PossiblyWrappedTypedArrayHasDetachedBuffer(obj)) {
        ThrowTypeError(JSMSG_TYPED_ARRAY_DETACHED);
      }
      if (PossiblyWrappedTypedArrayHasImmutableBuffer(obj)) {
        ThrowTypeError(JSMSG_ARRAYBUFFER_IMMUTABLE);
      }
      return;
    }
  }

  /* Steps 1-2. */
  ThrowTypeError(JSMSG_NON_TYPED_ARRAY_RETURNED);
}

// ES2017 draft rev 6859bb9ccaea9c6ede81d71e5320e3833b92cb3e
// 22.2.4.6 TypedArrayCreate ( constructor, argumentList )
function TypedArrayCreateWithLength(constructor, length) {
  // Step 1.
  var newTypedArray = constructContentFunction(
    constructor,
    constructor,
    length
  );

  // Step 2.
  ValidateWritableTypedArray(newTypedArray);

  // Step 3.
  var len = PossiblyWrappedTypedArrayLength(newTypedArray);

  if (len < length) {
    ThrowTypeError(JSMSG_SHORT_TYPED_ARRAY_RETURNED, length, len);
  }

  // Step 4.
  return newTypedArray;
}

// ES2017 draft rev 6859bb9ccaea9c6ede81d71e5320e3833b92cb3e
// 22.2.4.7 TypedArraySpeciesCreate ( exemplar, argumentList )
function TypedArraySpeciesCreateWithLength(exemplar, length) {
  // Step 1 (omitted).

  // Steps 2-3.
  var C = TypedArraySpeciesConstructor(exemplar);

  // Step 4.
  return TypedArrayCreateWithLength(C, length);
}

// ES6 draft rev30 (2014/12/24) 22.2.3.6 %TypedArray%.prototype.entries()
function TypedArrayEntries() {
  // Step 1.
  var O = this;

  // We need to be a bit careful here, because in the Xray case we want to
  // create the iterator in our current compartment.
  //
  // Before doing that, though, we want to check that we have a typed array
  // and it does not have a detached array buffer.  We do the latter by just
  // calling EnsureAttachedArrayBuffer() and letting it throw if there isn't one.
  // In the case when we're not sure we have a typed array (e.g. we might have
  // a cross-compartment wrapper for one), we can go ahead and call
  // EnsureAttachedArrayBuffer via EnsureTypedArrayWithArrayBuffer; that will
  // throw if we're not actually a wrapped typed array, or if we have a
  // detached array buffer.

  // Step 2-6.
  EnsureTypedArrayWithArrayBuffer(O);

  // We also need to make sure the length is in-bounds. This is checked by
  // calling PossiblyWrappedTypedArrayLength, which throws for out-of-bounds.
  PossiblyWrappedTypedArrayLength(O);

  // Step 7.
  return CreateArrayIterator(O, ITEM_KIND_KEY_AND_VALUE);
}

// ES2021 draft rev 190d474c3d8728653fbf8a5a37db1de34b9c1472
// Plus <https://github.com/tc39/ecma262/pull/2221>
// 22.2.3.7 %TypedArray%.prototype.every ( callbackfn [ , thisArg ] )
function TypedArrayEvery(callbackfn /*, thisArg*/) {
  // Step 1.
  var O = this;

  // Step 2.
  EnsureTypedArrayWithArrayBuffer(O);

  // If we got here, `this` is either a typed array or a wrapper for one.

  // Step 3.
  var len = PossiblyWrappedTypedArrayLength(O);

  // Step 4.
  if (ArgumentsLength() === 0) {
    ThrowTypeError(JSMSG_MISSING_FUN_ARG, 0, "%TypedArray%.prototype.every");
  }
  if (!IsCallable(callbackfn)) {
    ThrowTypeError(JSMSG_NOT_FUNCTION, DecompileArg(0, callbackfn));
  }

  var thisArg = ArgumentsLength() > 1 ? GetArgument(1) : undefined;

  // Steps 5-6.
  for (var k = 0; k < len; k++) {
    // Steps 6.b-d.
    var kValue = O[k];

    // Step 6.c.
    var testResult = callContentFunction(callbackfn, thisArg, kValue, k, O);

    // Step 6.d.
    if (!testResult) {
      return false;
    }
  }

  // Step 7.
  return true;
}
// Inlining this enables inlining of the callback function.
SetIsInlinableLargeFunction(TypedArrayEvery);

// ES2017 draft rev 6859bb9ccaea9c6ede81d71e5320e3833b92cb3e
// %TypedArray%.prototype.filter ( callbackfn [ , thisArg ] )
function TypedArrayFilter(callbackfn /*, thisArg*/) {
  // Step 1.
  var O = this;

  // Step 2.
  // This function is not generic.
  // We want to make sure that we have an attached buffer, per spec prose.
  EnsureTypedArrayWithArrayBuffer(O);

  // If we got here, `this` is either a typed array or a wrapper for one.

  // Step 3.
  var len = PossiblyWrappedTypedArrayLength(O);

  // Step 4.
  if (ArgumentsLength() === 0) {
    ThrowTypeError(JSMSG_MISSING_FUN_ARG, 0, "%TypedArray%.prototype.filter");
  }
  if (!IsCallable(callbackfn)) {
    ThrowTypeError(JSMSG_NOT_FUNCTION, DecompileArg(0, callbackfn));
  }

  // Step 5.
  var T = ArgumentsLength() > 1 ? GetArgument(1) : undefined;

  // Step 6.
  var kept = new_List();

  // Step 8.
  var captured = 0;

  // Steps 7 and 9.e.
  for (var k = 0; k < len; k++) {
    // Steps 9.a-b.
    var kValue = O[k];

    // Steps 9.c-d.
    if (callContentFunction(callbackfn, T, kValue, k, O)) {
      // Steps 9.d.i-ii.
      kept[captured++] = kValue;
    }
  }

  // Step 10.
  var A = TypedArraySpeciesCreateWithLength(O, captured);

  // Steps 11 and 12.b.
  for (var n = 0; n < captured; n++) {
    // Step 12.a.
    A[n] = kept[n];
  }

  // Step 13.
  return A;
}
// Inlining this enables inlining of the callback function.
SetIsInlinableLargeFunction(TypedArrayFilter);

// ES2021 draft rev 190d474c3d8728653fbf8a5a37db1de34b9c1472
// Plus <https://github.com/tc39/ecma262/pull/2221>
// 22.2.3.10 %TypedArray%.prototype.find ( predicate [ , thisArg ] )
function TypedArrayFind(predicate /*, thisArg*/) {
  // Step 1.
  var O = this;

  // Step 2.
  EnsureTypedArrayWithArrayBuffer(O);

  // If we got here, `this` is either a typed array or a wrapper for one.

  // Step 3.
  var len = PossiblyWrappedTypedArrayLength(O);

  // Step 4.
  if (ArgumentsLength() === 0) {
    ThrowTypeError(JSMSG_MISSING_FUN_ARG, 0, "%TypedArray%.prototype.find");
  }
  if (!IsCallable(predicate)) {
    ThrowTypeError(JSMSG_NOT_FUNCTION, DecompileArg(0, predicate));
  }

  var thisArg = ArgumentsLength() > 1 ? GetArgument(1) : undefined;

  // Steps 5-6.
  for (var k = 0; k < len; k++) {
    // Steps 6.a-b.
    var kValue = O[k];

    // Steps 6.c-d.
    if (callContentFunction(predicate, thisArg, kValue, k, O)) {
      return kValue;
    }
  }

  // Step 7.
  return undefined;
}
// Inlining this enables inlining of the callback function.
SetIsInlinableLargeFunction(TypedArrayFind);

// ES2021 draft rev 190d474c3d8728653fbf8a5a37db1de34b9c1472
// Plus <https://github.com/tc39/ecma262/pull/2221>
// 22.2.3.11 %TypedArray%.prototype.findIndex ( predicate [ , thisArg ] )
function TypedArrayFindIndex(predicate /*, thisArg*/) {
  // Step 1.
  var O = this;

  // Step 2.
  EnsureTypedArrayWithArrayBuffer(O);

  // If we got here, `this` is either a typed array or a wrapper for one.

  // Step 3.
  var len = PossiblyWrappedTypedArrayLength(O);

  // Step 4.
  if (ArgumentsLength() === 0) {
    ThrowTypeError(
      JSMSG_MISSING_FUN_ARG,
      0,
      "%TypedArray%.prototype.findIndex"
    );
  }
  if (!IsCallable(predicate)) {
    ThrowTypeError(JSMSG_NOT_FUNCTION, DecompileArg(0, predicate));
  }

  var thisArg = ArgumentsLength() > 1 ? GetArgument(1) : undefined;

  // Steps 5-6.
  for (var k = 0; k < len; k++) {
    // Steps 6.a-f.
    if (callContentFunction(predicate, thisArg, O[k], k, O)) {
      return k;
    }
  }

  // Step 7.
  return -1;
}
// Inlining this enables inlining of the callback function.
SetIsInlinableLargeFunction(TypedArrayFindIndex);

// ES2021 draft rev 190d474c3d8728653fbf8a5a37db1de34b9c1472
// Plus <https://github.com/tc39/ecma262/pull/2221>
// 22.2.3.12 %TypedArray%.prototype.forEach ( callbackfn [ , thisArg ] )
function TypedArrayForEach(callbackfn /*, thisArg*/) {
  // Step 1.
  var O = this;

  // Step 2.
  EnsureTypedArrayWithArrayBuffer(O);

  // If we got here, `this` is either a typed array or a wrapper for one.

  // Step 3.
  var len = PossiblyWrappedTypedArrayLength(O);

  // Step 4.
  if (ArgumentsLength() === 0) {
    ThrowTypeError(JSMSG_MISSING_FUN_ARG, 0, "TypedArray.prototype.forEach");
  }
  if (!IsCallable(callbackfn)) {
    ThrowTypeError(JSMSG_NOT_FUNCTION, DecompileArg(0, callbackfn));
  }

  var thisArg = ArgumentsLength() > 1 ? GetArgument(1) : undefined;

  // Steps 5-6.
  for (var k = 0; k < len; k++) {
    // Steps 6.a-c.
    callContentFunction(callbackfn, thisArg, O[k], k, O);
  }

  // Step 7.
  return undefined;
}
// Inlining this enables inlining of the callback function.
SetIsInlinableLargeFunction(TypedArrayForEach);

// ES6 draft (2016/1/11) 22.2.3.15 %TypedArray%.prototype.keys()
function TypedArrayKeys() {
  // Step 1.
  var O = this;

  // See the big comment in TypedArrayEntries for what we're doing here.

  // Step 2.
  EnsureTypedArrayWithArrayBuffer(O);
  PossiblyWrappedTypedArrayLength(O);

  // Step 3.
  return CreateArrayIterator(O, ITEM_KIND_KEY);
}

// ES2017 draft rev 6859bb9ccaea9c6ede81d71e5320e3833b92cb3e
// 22.2.3.19 %TypedArray%.prototype.map ( callbackfn [ , thisArg ] )
function TypedArrayMap(callbackfn /*, thisArg*/) {
  // Step 1.
  var O = this;

  // Step 2.
  // This function is not generic.
  // We want to make sure that we have an attached buffer, per spec prose.
  EnsureTypedArrayWithArrayBuffer(O);

  // If we got here, `this` is either a typed array or a wrapper for one.

  // Step 3.
  var len = PossiblyWrappedTypedArrayLength(O);

  // Step 4.
  if (ArgumentsLength() === 0) {
    ThrowTypeError(JSMSG_MISSING_FUN_ARG, 0, "%TypedArray%.prototype.map");
  }
  if (!IsCallable(callbackfn)) {
    ThrowTypeError(JSMSG_NOT_FUNCTION, DecompileArg(0, callbackfn));
  }

  // Step 5.
  var T = ArgumentsLength() > 1 ? GetArgument(1) : undefined;

  // Step 6.
  var A = TypedArraySpeciesCreateWithLength(O, len);

  // Steps 7, 8.a (implicit) and 8.e.
  for (var k = 0; k < len; k++) {
    // Steps 8.b-c.
    var mappedValue = callContentFunction(callbackfn, T, O[k], k, O);

    // Steps 8.d.
    A[k] = mappedValue;
  }

  // Step 9.
  return A;
}
// Inlining this enables inlining of the callback function.
SetIsInlinableLargeFunction(TypedArrayMap);

// ES2021 draft rev 190d474c3d8728653fbf8a5a37db1de34b9c1472
// Plus <https://github.com/tc39/ecma262/pull/2221>
// 22.2.3.20 %TypedArray%.prototype.reduce ( callbackfn [ , initialValue ] )
function TypedArrayReduce(callbackfn /*, initialValue*/) {
  // Step 1.
  var O = this;

  // Step 2.
  EnsureTypedArrayWithArrayBuffer(O);

  // If we got here, `this` is either a typed array or a wrapper for one.

  // Step 3.
  var len = PossiblyWrappedTypedArrayLength(O);

  // Step 4.
  if (ArgumentsLength() === 0) {
    ThrowTypeError(JSMSG_MISSING_FUN_ARG, 0, "%TypedArray%.prototype.reduce");
  }
  if (!IsCallable(callbackfn)) {
    ThrowTypeError(JSMSG_NOT_FUNCTION, DecompileArg(0, callbackfn));
  }

  // Step 5.
  if (len === 0 && ArgumentsLength() === 1) {
    ThrowTypeError(JSMSG_EMPTY_ARRAY_REDUCE);
  }

  // Step 6.
  var k = 0;

  // Steps 7-9.
  var accumulator = ArgumentsLength() > 1 ? GetArgument(1) : O[k++];

  // Step 10.
  for (; k < len; k++) {
    accumulator = callContentFunction(
      callbackfn,
      undefined,
      accumulator,
      O[k],
      k,
      O
    );
  }

  // Step 11.
  return accumulator;
}

// ES2021 draft rev 190d474c3d8728653fbf8a5a37db1de34b9c1472
// Plus <https://github.com/tc39/ecma262/pull/2221>
// 22.2.3.21 %TypedArray%.prototype.reduceRight ( callbackfn [ , initialValue ] )
function TypedArrayReduceRight(callbackfn /*, initialValue*/) {
  // Step 1.
  var O = this;

  // Step 2.
  EnsureTypedArrayWithArrayBuffer(O);

  // If we got here, `this` is either a typed array or a wrapper for one.

  // Step 3.
  var len = PossiblyWrappedTypedArrayLength(O);

  // Step 4.
  if (ArgumentsLength() === 0) {
    ThrowTypeError(
      JSMSG_MISSING_FUN_ARG,
      0,
      "%TypedArray%.prototype.reduceRight"
    );
  }
  if (!IsCallable(callbackfn)) {
    ThrowTypeError(JSMSG_NOT_FUNCTION, DecompileArg(0, callbackfn));
  }

  // Step 5.
  if (len === 0 && ArgumentsLength() === 1) {
    ThrowTypeError(JSMSG_EMPTY_ARRAY_REDUCE);
  }

  // Step 6.
  var k = len - 1;

  // Steps 7-9.
  var accumulator = ArgumentsLength() > 1 ? GetArgument(1) : O[k--];

  // Step 10.
  for (; k >= 0; k--) {
    accumulator = callContentFunction(
      callbackfn,
      undefined,
      accumulator,
      O[k],
      k,
      O
    );
  }

  // Step 11.
  return accumulator;
}

// ES2021 draft rev 190d474c3d8728653fbf8a5a37db1de34b9c1472
// Plus <https://github.com/tc39/ecma262/pull/2221>
// 22.2.3.25 %TypedArray%.prototype.some ( callbackfn [ , thisArg ] )
function TypedArraySome(callbackfn /*, thisArg*/) {
  // Step 1.
  var O = this;

  // Step 2.
  EnsureTypedArrayWithArrayBuffer(O);

  // If we got here, `this` is either a typed array or a wrapper for one.

  // Step 3.
  var len = PossiblyWrappedTypedArrayLength(O);

  // Step 4.
  if (ArgumentsLength() === 0) {
    ThrowTypeError(JSMSG_MISSING_FUN_ARG, 0, "%TypedArray%.prototype.some");
  }
  if (!IsCallable(callbackfn)) {
    ThrowTypeError(JSMSG_NOT_FUNCTION, DecompileArg(0, callbackfn));
  }

  var thisArg = ArgumentsLength() > 1 ? GetArgument(1) : undefined;

  // Steps 5-6.
  for (var k = 0; k < len; k++) {
    // Steps 6.a-b.
    var kValue = O[k];

    // Step 6.c.
    var testResult = callContentFunction(callbackfn, thisArg, kValue, k, O);

    // Step 6.d.
    if (testResult) {
      return true;
    }
  }

  // Step 7.
  return false;
}
// Inlining this enables inlining of the callback function.
SetIsInlinableLargeFunction(TypedArraySome);

// ES2017 draft rev f8a9be8ea4bd97237d176907a1e3080dce20c68f
//   22.2.3.28 %TypedArray%.prototype.toLocaleString ([ reserved1 [ , reserved2 ] ])
// ES2017 Intl draft rev 78bbe7d1095f5ff3760ac4017ed366026e4cb276
//   13.4.1 Array.prototype.toLocaleString ([ locales [ , options ]])
function TypedArrayToLocaleString(locales = undefined, options = undefined) {
  // ValidateTypedArray, then step 1.
  var array = this;

  // This function is not generic.
  // We want to make sure that we have an attached buffer, per spec prose.
  EnsureTypedArrayWithArrayBuffer(array);

  // If we got here, `this` is either a typed array or a wrapper for one.

  // Step 2.
  var len = PossiblyWrappedTypedArrayLength(array);

  // Step 4.
  if (len === 0) {
    return "";
  }

  // Step 5.
  var firstElement = array[0];
  assert(
    typeof firstElement === "number" || typeof firstElement === "bigint",
    "TypedArray elements are either Numbers or BigInts"
  );

  // Steps 6-7.
  // Omit the 'if' clause in step 6, since non-empty typed arrays can't have
  // undefined or null elements.
  #if JS_HAS_INTL_API
  var R = ToString(
    callContentFunction(
      firstElement.toLocaleString,
      firstElement,
      locales,
      options
    )
  );
  #else
  var R = ToString(
    callContentFunction(firstElement.toLocaleString, firstElement)
  );
  #endif

  // Step 3 (reordered).
  // We don't (yet?) implement locale-dependent separators.
  var separator = ",";

  // Steps 8-9.
  for (var k = 1; k < len; k++) {
    // Step 9.a.
    R += separator;

    // Step 9.b.
    var nextElement = array[k];

    // Step 9.c.
    if (nextElement === undefined) {
      continue;
    }
    assert(
      typeof nextElement === "number" || typeof nextElement === "bigint",
      "TypedArray elements are either Numbers or BigInts"
    );

    // Steps 9.d-e.
    #if JS_HAS_INTL_API
    R += ToString(
      callContentFunction(
        nextElement.toLocaleString,
        nextElement,
        locales,
        options
      )
    );
    #else
    R += ToString(callContentFunction(nextElement.toLocaleString, nextElement));
    #endif
  }

  // Step 10.
  return R;
}

// https://tc39.es/proposal-relative-indexing-method
// %TypedArray%.prototype.at ( index )
function TypedArrayAt(index) {
  // Step 1.
  var obj = this;

  // Step 2.
  // This function is not generic.
  if (!IsObject(obj) || !IsTypedArray(obj)) {
    return callFunction(
      CallTypedArrayMethodIfWrapped,
      obj,
      index,
      "TypedArrayAt"
    );
  }
  EnsureAttachedArrayBuffer(obj);

  // Step 3.
  var len = TypedArrayLength(obj);

  // Step 4.
  var relativeIndex = ToInteger(index);

  // Steps 5-6.
  var k;
  if (relativeIndex >= 0) {
    k = relativeIndex;
  } else {
    k = len + relativeIndex;
  }

  // Step 7.
  if (k < 0 || k >= len) {
    return undefined;
  }

  // Step 8.
  return obj[k];
}
// This function is only barely too long for normal inlining.
SetIsInlinableLargeFunction(TypedArrayAt);

// https://github.com/tc39/proposal-array-find-from-last
// %TypedArray%.prototype.findLast ( predicate, thisArg )
function TypedArrayFindLast(predicate /*, thisArg*/) {
  // Step 1.
  var O = this;

  // Step 2.
  EnsureTypedArrayWithArrayBuffer(O);

  // If we got here, `this` is either a typed array or a wrapper for one.

  // Step 3.
  var len = PossiblyWrappedTypedArrayLength(O);

  // Step 4.
  if (ArgumentsLength() === 0) {
    ThrowTypeError(JSMSG_MISSING_FUN_ARG, 0, "%TypedArray%.prototype.findLast");
  }
  if (!IsCallable(predicate)) {
    ThrowTypeError(JSMSG_NOT_FUNCTION, DecompileArg(0, predicate));
  }

  var thisArg = ArgumentsLength() > 1 ? GetArgument(1) : undefined;

  // Steps 5-6.
  for (var k = len - 1; k >= 0; k--) {
    // Steps 6.a-b.
    var kValue = O[k];

    // Steps 6.c-d.
    if (callContentFunction(predicate, thisArg, kValue, k, O)) {
      return kValue;
    }
  }

  // Step 7.
  return undefined;
}
// Inlining this enables inlining of the callback function.
SetIsInlinableLargeFunction(TypedArrayFindLast);

// https://github.com/tc39/proposal-array-find-from-last
// %TypedArray%.prototype.findLastIndex ( predicate, thisArg )
function TypedArrayFindLastIndex(predicate /*, thisArg*/) {
  // Step 1.
  var O = this;

  // Step 2.
  EnsureTypedArrayWithArrayBuffer(O);

  // If we got here, `this` is either a typed array or a wrapper for one.

  // Step 3.
  var len = PossiblyWrappedTypedArrayLength(O);

  // Step 4.
  if (ArgumentsLength() === 0) {
    ThrowTypeError(
      JSMSG_MISSING_FUN_ARG,
      0,
      "%TypedArray%.prototype.findLastIndex"
    );
  }
  if (!IsCallable(predicate)) {
    ThrowTypeError(JSMSG_NOT_FUNCTION, DecompileArg(0, predicate));
  }

  var thisArg = ArgumentsLength() > 1 ? GetArgument(1) : undefined;

  // Steps 5-6.
  for (var k = len - 1; k >= 0; k--) {
    // Steps 6.a-f.
    if (callContentFunction(predicate, thisArg, O[k], k, O)) {
      return k;
    }
  }

  // Step 7.
  return -1;
}
// Inlining this enables inlining of the callback function.
SetIsInlinableLargeFunction(TypedArrayFindLastIndex);

// ES6 draft rev30 (2014/12/24) 22.2.3.30 %TypedArray%.prototype.values()
//
// Uncloned functions with `$` prefix are allocated as extended function
// to store the original name in `SetCanonicalName`.
function $TypedArrayValues() {
  // Step 1.
  var O = this;

  // See the big comment in TypedArrayEntries for what we're doing here.
  EnsureTypedArrayWithArrayBuffer(O);
  PossiblyWrappedTypedArrayLength(O);

  // Step 7.
  return CreateArrayIterator(O, ITEM_KIND_VALUE);
}
SetCanonicalName($TypedArrayValues, "values");

// ES2017 draft rev 6859bb9ccaea9c6ede81d71e5320e3833b92cb3e
// 22.2.2.1 %TypedArray%.from ( source [ , mapfn [ , thisArg ] ] )
function TypedArrayStaticFrom(source, mapfn = undefined, thisArg = undefined) {
  // Step 1.
  var C = this;

  // Step 2.
  if (!IsConstructor(C)) {
    ThrowTypeError(JSMSG_NOT_CONSTRUCTOR, typeof C);
  }

  // Step 3.
  var mapping;
  if (mapfn !== undefined) {
    // Step 3.a.
    if (!IsCallable(mapfn)) {
      ThrowTypeError(JSMSG_NOT_FUNCTION, DecompileArg(1, mapfn));
    }

    // Step 3.b.
    mapping = true;
  } else {
    // Step 4.
    mapping = false;
  }

  // Step 5.
  var T = thisArg;

  // Step 6.
  // Inlined: GetMethod, steps 1-2.
  var usingIterator = source[GetBuiltinSymbol("iterator")];

  // Step 7.
  // Inlined: GetMethod, step 3.
  if (usingIterator !== undefined && usingIterator !== null) {
    // Inlined: GetMethod, step 4.
    if (!IsCallable(usingIterator)) {
      ThrowTypeError(JSMSG_NOT_ITERABLE, DecompileArg(0, source));
    }

    // Try to take a fast path when there's no mapper function and the
    // constructor is a built-in TypedArray constructor.
    if (!mapping && IsTypedArrayConstructor(C) && IsObject(source)) {
      // The source is a TypedArray using the default iterator.
      if (
        usingIterator === $TypedArrayValues &&
        IsTypedArray(source) &&
        ArrayIteratorPrototypeOptimizable()
      ) {
        // Step 7.a.
        // Omitted but we still need to throw if |source| was detached.
        EnsureAttachedArrayBuffer(source);

        // Step 7.b.
        var len = TypedArrayLength(source);

        // Step 7.c.
        var targetObj = constructContentFunction(C, C, len);

        // Steps 7.d-f.
        for (var k = 0; k < len; k++) {
          targetObj[k] = source[k];
        }

        // Step 7.g.
        return targetObj;
      }

      // The source is a packed array using the default iterator.
      if (
        usingIterator === $ArrayValues &&
        IsPackedArray(source) &&
        ArrayIteratorPrototypeOptimizable()
      ) {
        // Steps 7.b-c.
        var targetObj = constructContentFunction(C, C, source.length);

        // Steps 7.a, 7.d-f.
        TypedArrayInitFromPackedArray(targetObj, source);

        // Step 7.g.
        return targetObj;
      }
    }

    // Step 7.a.
    var values = IterableToList(source, usingIterator);

    // Step 7.b.
    var len = values.length;

    // Step 7.c.
    var targetObj = TypedArrayCreateWithLength(C, len);

    // Steps 7.d-e.
    for (var k = 0; k < len; k++) {
      // Step 7.e.ii.
      var kValue = values[k];

      // Steps 7.e.iii-iv.
      var mappedValue = mapping
        ? callContentFunction(mapfn, T, kValue, k)
        : kValue;

      // Step 7.e.v.
      targetObj[k] = mappedValue;
    }

    // Step 7.f.
    // Asserting that `values` is empty here would require removing them one by one from
    // the list's start in the loop above. That would introduce unacceptable overhead.
    // Additionally, the loop's logic is simple enough not to require the assert.

    // Step 7.g.
    return targetObj;
  }

  // Step 8 is an assertion: items is not an Iterator. Testing this is
  // literally the very last thing we did, so we don't assert here.

  // Step 9.
  var arrayLike = ToObject(source);

  // Step 10.
  var len = ToLength(arrayLike.length);

  // Step 11.
  var targetObj = TypedArrayCreateWithLength(C, len);

  // Steps 12-13.
  for (var k = 0; k < len; k++) {
    // Steps 13.a-b.
    var kValue = arrayLike[k];

    // Steps 13.c-d.
    var mappedValue = mapping
      ? callContentFunction(mapfn, T, kValue, k)
      : kValue;

    // Step 13.e.
    targetObj[k] = mappedValue;
  }

  // Step 14.
  return targetObj;
}

// ES2017 draft rev 6859bb9ccaea9c6ede81d71e5320e3833b92cb3e
// 22.2.2.2 %TypedArray%.of ( ...items )
function TypedArrayStaticOf(/*...items*/) {
  // Step 1.
  var len = ArgumentsLength();

  // Step 2 (implicit).

  // Step 3.
  var C = this;

  // Step 4.
  if (!IsConstructor(C)) {
    ThrowTypeError(JSMSG_NOT_CONSTRUCTOR, typeof C);
  }

  // Step 5.
  var newObj = TypedArrayCreateWithLength(C, len);

  // Steps 6-7.
  for (var k = 0; k < len; k++) {
    newObj[k] = GetArgument(k);
  }

  // Step 8.
  return newObj;
}

// ES 2016 draft Mar 25, 2016 22.2.2.4.
function $TypedArraySpecies() {
  // Step 1.
  return this;
}
SetCanonicalName($TypedArraySpecies, "get [Symbol.species]");

// ES2018 draft rev 0525bb33861c7f4e9850f8a222c89642947c4b9c
// 22.2.2.1.1 Runtime Semantics: IterableToList( items, method )
function IterableToList(items, method) {
  // Step 1 (Inlined GetIterator).

  // 7.4.1 GetIterator, step 1.
  assert(IsCallable(method), "method argument is a function");

  // 7.4.1 GetIterator, step 2.
  var iterator = callContentFunction(method, items);

  // 7.4.1 GetIterator, step 3.
  if (!IsObject(iterator)) {
    ThrowTypeError(JSMSG_GET_ITER_RETURNED_PRIMITIVE);
  }

  // 7.4.1 GetIterator, step 4.
  var nextMethod = iterator.next;

  // Step 2.
  var values = [];

  // Steps 3-4.
  var i = 0;
  while (true) {
    // Step 4.a.
    var next = callContentFunction(nextMethod, iterator);
    if (!IsObject(next)) {
      ThrowTypeError(JSMSG_ITER_METHOD_RETURNED_PRIMITIVE, "next");
    }

    // Step 4.b.
    if (next.done) {
      break;
    }
    DefineDataProperty(values, i++, next.value);
  }

  // Step 5.
  return values;
}

// ES 2016 draft Mar 25, 2016 24.1.3.3.
function $ArrayBufferSpecies() {
  // Step 1.
  return this;
}
SetCanonicalName($ArrayBufferSpecies, "get [Symbol.species]");

// Shared memory and atomics proposal (30 Oct 2016)
function $SharedArrayBufferSpecies() {
  // Step 1.
  return this;
}
SetCanonicalName($SharedArrayBufferSpecies, "get [Symbol.species]");

// https://github.com/tc39/proposal-change-array-by-copy
function TypedArrayCreateSameType(exemplar, length) {
  // Step 1. Assert: exemplar is an Object that has [[TypedArrayName]] and [[ContentType]] internal slots.
  assert(
    IsPossiblyWrappedTypedArray(exemplar),
    "in TypedArrayCreateSameType, exemplar does not have a [[ContentType]] internal slot"
  );

  // Step 2. Let constructor be the intrinsic object listed in column one of Table 63 for exemplar.[[TypedArrayName]].
  var constructor = ConstructorForTypedArray(exemplar);

  // Step 4 omitted. Assert: result has [[TypedArrayName]] and [[ContentType]] internal slots. - guaranteed by the TypedArray implementation
  // Step 5 omitted. Assert: result.[[ContentType]] is exemplar.[[ContentType]]. - guaranteed by the typed array implementation

  // Step 3. Let result be ? TypedArrayCreate(constructor, argumentList).
  // Step 6. Return result.
  return TypedArrayCreateWithLength(constructor, length);
}

// https://tc39.es/ecma262/#sec-%typedarray%.prototype.tosorted
// 23.2.3.33 %TypedArray%.prototype.toSorted ( comparefn )
function TypedArrayToSorted(comparefn) {
  // Step 1. If comparefn is not undefined and IsCallable(comparefn) is false,
  // throw a TypeError exception.
  if (comparefn !== undefined) {
    if (!IsCallable(comparefn)) {
      ThrowTypeError(JSMSG_NOT_FUNCTION, DecompileArg(0, comparefn));
    }
  }

  // Step 2. Let O be the this value.
  var O = this;

  // Step 3. Perform ? ValidateTypedArray(this).
  EnsureTypedArrayWithArrayBuffer(O);

  // Step 4. Let len be TypedArrayLength(taRecord).
  var len = PossiblyWrappedTypedArrayLength(O);

  // Step 5. Let A be ? TypedArrayCreateSameType(O, « 𝔽(len) »).
  var A = TypedArrayCreateSameType(O, len);

  // Steps 6-10 not followed exactly; this implementation copies the list and then
  // sorts the copy, rather than calling a sort method that copies the list and then
  // copying the result again.

  // Equivalent to steps 9-10.
  for (var k = 0; k < len; k++) {
    A[k] = O[k];
  }

  // Arrays with less than two elements remain unchanged when sorted.
  if (len > 1) {
    // Equivalent to steps 6-8.
    callFunction(std_TypedArray_sort, A, comparefn);
  }

  // Step 11.
  return A;
}
