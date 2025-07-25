/* eslint-disable mozilla/no-comparison-or-assignment-inside-ok */

importScripts("../../tests/mochitest/general/interface_exposure_checker.js");

// This is a list of all interfaces that are exposed to workers.
// Please only add things to this list with great care and proper review
// from the associated module peers.

// This file lists global interfaces we want exposed and verifies they
// are what we intend. Each entry in the arrays below can either be a
// simple string with the interface name, or an object with a 'name'
// property giving the interface name as a string, and additional
// properties which qualify the exposure of that interface. For example:
//
// [
//   "AGlobalInterface", // secure context only
//   { name: "DesktopOnlyThing", desktop: true },
//   { name: "DisabledEverywhere", disabled: true },
//   { name: "ExperimentalThing", release: false },
//   { name: "ReallyExperimentalThing", nightly: true },
// ];
//
// Note that the items are alphabetically sorted. This is a requirement.
// See createInterfaceMap() in interface_exposure_checker.js for a complete
// list of properties.

// IMPORTANT: Do not change this list without review from
//            a JavaScript Engine peer!
let wasmGlobalEntry = {
  name: "WebAssembly",
  insecureContext: true,
  disabled: !getJSTestingFunctions().wasmIsSupportedByHardware(),
};
let wasmGlobalInterfaces = [
  { name: "CompileError", insecureContext: true },
  { name: "Exception", insecureContext: true },
  { name: "Function", insecureContext: true, nightly: true },
  { name: "Global", insecureContext: true },
  { name: "Instance", insecureContext: true },
  { name: "JSTag", insecureContext: true },
  { name: "LinkError", insecureContext: true },
  { name: "Memory", insecureContext: true },
  { name: "Module", insecureContext: true },
  { name: "RuntimeError", insecureContext: true },
  { name: "Table", insecureContext: true },
  { name: "Tag", insecureContext: true },
  { name: "compile", insecureContext: true },
  { name: "compileStreaming", insecureContext: true },
  { name: "instantiate", insecureContext: true },
  { name: "instantiateStreaming", insecureContext: true },
  { name: "validate", insecureContext: true },
];
// IMPORTANT: Do not change this list without review from
//            a JavaScript Engine peer!
let ecmaGlobals = [
  "AggregateError",
  "Array",
  "ArrayBuffer",
  "AsyncDisposableStack",
  "Atomics",
  "BigInt",
  "BigInt64Array",
  "BigUint64Array",
  "Boolean",
  "DataView",
  "Date",
  "DisposableStack",
  "Error",
  "EvalError",
  "FinalizationRegistry",
  "Float16Array",
  "Float32Array",
  "Float64Array",
  "Function",
  "Infinity",
  "Int16Array",
  "Int32Array",
  "Int8Array",
  "InternalError",
  "Intl",
  "Iterator",
  "JSON",
  "Map",
  "Math",
  "NaN",
  "Number",
  "Object",
  "Promise",
  "Proxy",
  "RangeError",
  "ReferenceError",
  "Reflect",
  "RegExp",
  "Set",
  { name: "SharedArrayBuffer", crossOriginIsolated: true },
  "String",
  "SuppressedError",
  "Symbol",
  "SyntaxError",
  "Temporal",
  "TypeError",
  "URIError",
  "Uint16Array",
  "Uint32Array",
  "Uint8Array",
  "Uint8ClampedArray",
  "WeakMap",
  "WeakRef",
  "WeakSet",
  wasmGlobalEntry,
  "decodeURI",
  "decodeURIComponent",
  "encodeURI",
  "encodeURIComponent",
  "escape",
  "eval",
  "globalThis",
  "isFinite",
  "isNaN",
  "parseFloat",
  "parseInt",
  "undefined",
  "unescape",
];
// IMPORTANT: Do not change the list above without review from
//            a JavaScript Engine peer!

// IMPORTANT: Do not change the list below without review from a DOM peer!
let interfaceNamesInGlobalScope = [
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "AbortController",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "AbortSignal",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "Blob",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "BroadcastChannel",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "ByteLengthQueuingStrategy",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "Cache",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "CacheStorage",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "CanvasGradient",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "CanvasPattern",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "Client",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "Clients",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "CloseEvent",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "CompressionStream",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "CookieStore",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "CookieStoreManager",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "CountQueuingStrategy",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "Crypto",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "CryptoKey",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "CustomEvent",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "DOMException",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "DOMMatrix",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "DOMMatrixReadOnly",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "DOMPoint",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "DOMPointReadOnly",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "DOMQuad",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "DOMRect",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "DOMRectReadOnly",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "DOMStringList",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "DecompressionStream",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "Directory",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "ErrorEvent",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "Event",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "EventSource",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "EventTarget",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "ExtendableCookieChangeEvent",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "ExtendableEvent",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "ExtendableMessageEvent",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "FetchEvent",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "File",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "FileList",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "FileReader",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "FileSystemDirectoryHandle" },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "FileSystemFileHandle" },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "FileSystemHandle" },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "FileSystemWritableFileStream" },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "FontFace",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "FontFaceSet",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "FontFaceSetLoadEvent",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "FormData",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPU", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUAdapter", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUAdapterInfo", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUBindGroup", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUBindGroupLayout", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUBuffer", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUBufferUsage", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUCanvasContext", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUColorWrite", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUCommandBuffer", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUCommandEncoder", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUCompilationInfo", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUCompilationMessage", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUComputePassEncoder", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUComputePipeline", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUDevice", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUDeviceLostInfo", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUError", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUExternalTexture", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUInternalError", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUMapMode", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUOutOfMemoryError", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUPipelineError", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUPipelineLayout", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUQuerySet", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUQueue", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPURenderBundle", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPURenderBundleEncoder", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPURenderPassEncoder", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPURenderPipeline", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUSampler", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUShaderModule", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUShaderStage", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUSupportedFeatures", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUSupportedLimits", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUTexture", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUTextureUsage", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUTextureView", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUUncapturedErrorEvent", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "GPUValidationError", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "Headers",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "IDBCursor",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "IDBCursorWithValue",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "IDBDatabase",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "IDBFactory",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "IDBIndex",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "IDBKeyRange",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "IDBObjectStore",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "IDBOpenDBRequest",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "IDBRequest",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "IDBTransaction",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "IDBVersionChangeEvent",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "ImageBitmap",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "ImageBitmapRenderingContext",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "ImageData",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "Lock",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "LockManager",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "MediaCapabilities",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "MessageChannel",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "MessageEvent",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "MessagePort",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "NavigationPreloadManager",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "NetworkInformation", disabled: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "Notification",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "NotificationEvent",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "OffscreenCanvas",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "OffscreenCanvasRenderingContext2D",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "Path2D",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "Performance",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "PerformanceEntry",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "PerformanceMark",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "PerformanceMeasure",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "PerformanceObserver",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "PerformanceObserverEntryList",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "PerformanceResourceTiming",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "PerformanceServerTiming",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "PermissionStatus",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "Permissions",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "ProgressEvent",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "PromiseRejectionEvent",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "PushEvent",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "PushManager",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "PushMessageData",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "PushSubscription",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "PushSubscriptionChangeEvent",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "PushSubscriptionOptions",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "ReadableByteStreamController",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "ReadableStream",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "ReadableStreamBYOBReader",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "ReadableStreamBYOBRequest",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "ReadableStreamDefaultController",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "ReadableStreamDefaultReader",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "Request",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "Response",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "Scheduler",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "ServiceWorker",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "ServiceWorkerContainer",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "ServiceWorkerGlobalScope",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "ServiceWorkerRegistration",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "StorageManager", fennec: false },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "SubtleCrypto",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "TaskController",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "TaskPriorityChangeEvent",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "TaskSignal",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "TextDecoder",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "TextDecoderStream",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "TextEncoder",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "TextEncoderStream",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "TextMetrics",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "TransformStream",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "TransformStreamDefaultController",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "URL",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "URLPattern",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "URLSearchParams",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  { name: "WGSLLanguageFeatures", earlyBetaOrEarlier: true },
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "WebGL2RenderingContext",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "WebGLActiveInfo",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "WebGLBuffer",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "WebGLContextEvent",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "WebGLFramebuffer",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "WebGLProgram",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "WebGLQuery",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "WebGLRenderbuffer",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "WebGLRenderingContext",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "WebGLSampler",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "WebGLShader",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "WebGLShaderPrecisionFormat",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "WebGLSync",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "WebGLTexture",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "WebGLTransformFeedback",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "WebGLUniformLocation",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "WebGLVertexArrayObject",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "WebSocket",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "WebTransport",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "WebTransportBidirectionalStream",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "WebTransportDatagramDuplexStream",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "WebTransportError",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "WebTransportReceiveStream",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "WebTransportSendStream",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "WindowClient",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "WorkerGlobalScope",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "WorkerLocation",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "WorkerNavigator",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "WritableStream",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "WritableStreamDefaultController",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "WritableStreamDefaultWriter",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "clients",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "console",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "cookieStore",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "onactivate",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "oncookiechange",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "onfetch",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "oninstall",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "onmessage",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "onmessageerror",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "onnotificationclick",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "onnotificationclose",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "onpush",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "onpushsubscriptionchange",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "registration",
  // IMPORTANT: Do not change this list without review from a DOM peer!
  "skipWaiting",
  // IMPORTANT: Do not change this list without review from a DOM peer!
];
// IMPORTANT: Do not change the list above without review from a DOM peer!

// List of functions defined on the global by the test harness or this test
// file.
let testFunctions = [
  "ok",
  "is",
  "workerTestArrayEquals",
  "workerTestDone",
  "workerTestGetHelperData",
  "workerTestGetStorageManager",
  "entryDisabled",
  "createInterfaceMap",
  "runTest",
];

workerTestGetHelperData(function (data) {
  runTest("self", self, {
    data,
    testFunctions,
    interfaceGroups: [ecmaGlobals, interfaceNamesInGlobalScope],
  });
  if (WebAssembly && !entryDisabled(wasmGlobalEntry, data)) {
    runTest("WebAssembly", WebAssembly, {
      data,
      interfaceGroups: [wasmGlobalInterfaces],
    });
  }
  workerTestDone();
});
