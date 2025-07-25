/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/Credential.h"
#include "mozilla/dom/CredentialsContainer.h"
#include "mozilla/dom/FeaturePolicyUtils.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/StaticPrefs_security.h"
#include "mozilla/dom/WebIdentityHandler.h"
#include "mozilla/dom/WebAuthnHandler.h"
#include "mozilla/dom/WindowGlobalChild.h"
#include "mozilla/dom/WindowContext.h"
#include "nsContentUtils.h"
#include "nsIDocShell.h"

namespace mozilla::dom {

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(CredentialsContainer, mParent,
                                      mWebAuthnHandler)
NS_IMPL_CYCLE_COLLECTING_ADDREF(CredentialsContainer)
NS_IMPL_CYCLE_COLLECTING_RELEASE(CredentialsContainer)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(CredentialsContainer)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

already_AddRefed<Promise> CreatePromise(nsPIDOMWindowInner* aParent,
                                        ErrorResult& aRv) {
  MOZ_ASSERT(aParent);
  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(aParent);
  if (NS_WARN_IF(!global)) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }
  RefPtr<Promise> promise = Promise::Create(global, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }
  return promise.forget();
}

already_AddRefed<Promise> CreateAndRejectWithNotAllowed(
    nsPIDOMWindowInner* aParent, ErrorResult& aRv) {
  MOZ_ASSERT(aParent);
  RefPtr<Promise> promise = CreatePromise(aParent, aRv);
  if (!promise) {
    return nullptr;
  }
  promise->MaybeRejectWithNotAllowedError(
      "CredentialContainer request is not allowed."_ns);
  return promise.forget();
}

already_AddRefed<Promise> CreateAndRejectWithNotSupported(
    nsPIDOMWindowInner* aParent, ErrorResult& aRv) {
  MOZ_ASSERT(aParent);
  RefPtr<Promise> promise = CreatePromise(aParent, aRv);
  if (!promise) {
    return nullptr;
  }
  promise->MaybeRejectWithNotSupportedError(
      "CredentialContainer request is not supported."_ns);
  return promise.forget();
}

static bool IsInActiveTab(nsPIDOMWindowInner* aParent) {
  // Returns whether aParent is an inner window somewhere in the active tab.
  // The active tab is the selected (i.e. visible) tab in the focused window.
  MOZ_ASSERT(aParent);

  RefPtr<Document> doc = aParent->GetExtantDoc();
  if (NS_WARN_IF(!doc)) {
    return false;
  }

  return IsInActiveTab(doc);
}

static bool ConsumeUserActivation(nsPIDOMWindowInner* aParent) {
  // Returns whether aParent has transient activation, and consumes the
  // activation.
  MOZ_ASSERT(aParent);

  RefPtr<Document> doc = aParent->GetExtantDoc();
  if (NS_WARN_IF(!doc)) {
    return false;
  }

  return doc->ConsumeTransientUserGestureActivation();
}

// static
bool CredentialsContainer::IsSameOriginWithAncestors(
    nsPIDOMWindowInner* aParent) {
  // This method returns true if aParent is either not in a frame / iframe, or
  // is in a frame or iframe and all ancestors for aParent are the same origin.
  // This is useful for Credential Management because we need to prohibit
  // iframes, but not break mochitests (which use iframes to embed the tests).
  MOZ_ASSERT(aParent);

  WindowGlobalChild* wgc = aParent->GetWindowGlobalChild();

  // If there's no WindowGlobalChild, the inner window has already been
  // destroyed, so fail safe and return false.
  if (!wgc) {
    return false;
  }

  // Check that all ancestors are the same origin, repeating until we find a
  // null parent
  for (WindowContext* parentContext =
           wgc->WindowContext()->GetParentWindowContext();
       parentContext; parentContext = parentContext->GetParentWindowContext()) {
    if (!wgc->IsSameOriginWith(parentContext)) {
      // same-origin policy is violated
      return false;
    }
  }

  return true;
}

CredentialsContainer::CredentialsContainer(nsPIDOMWindowInner* aParent)
    : mParent(aParent) {
  MOZ_ASSERT(aParent);
}

CredentialsContainer::~CredentialsContainer() = default;

void CredentialsContainer::EnsureWebAuthnHandler() {
  MOZ_ASSERT(NS_IsMainThread());

  if (!mWebAuthnHandler) {
    mWebAuthnHandler = new WebAuthnHandler(mParent);
  }
}

already_AddRefed<WebAuthnHandler> CredentialsContainer::GetWebAuthnHandler() {
  MOZ_ASSERT(NS_IsMainThread());

  EnsureWebAuthnHandler();
  RefPtr<WebAuthnHandler> ref = mWebAuthnHandler;
  return ref.forget();
}

JSObject* CredentialsContainer::WrapObject(JSContext* aCx,
                                           JS::Handle<JSObject*> aGivenProto) {
  return CredentialsContainer_Binding::Wrap(aCx, this, aGivenProto);
}

already_AddRefed<Promise> CredentialsContainer::Get(
    const CredentialRequestOptions& aOptions, ErrorResult& aRv) {
  uint64_t totalOptions = 0;
  if (aOptions.mPublicKey.WasPassed() &&
      StaticPrefs::security_webauth_webauthn()) {
    totalOptions += 1;
  }
  if (aOptions.mIdentity.WasPassed() &&
      StaticPrefs::dom_security_credentialmanagement_identity_enabled()) {
    totalOptions += 1;
  }
  if (totalOptions > 1) {
    return CreateAndRejectWithNotSupported(mParent, aRv);
  }

  bool conditionallyMediated =
      aOptions.mMediation == CredentialMediationRequirement::Conditional;
  if (aOptions.mPublicKey.WasPassed() &&
      StaticPrefs::security_webauth_webauthn()) {
    MOZ_ASSERT(mParent);
    if (!FeaturePolicyUtils::IsFeatureAllowed(
            mParent->GetExtantDoc(), u"publickey-credentials-get"_ns) ||
        !(IsInActiveTab(mParent) || conditionallyMediated)) {
      return CreateAndRejectWithNotAllowed(mParent, aRv);
    }

    if (conditionallyMediated &&
        !StaticPrefs::security_webauthn_enable_conditional_mediation()) {
      RefPtr<Promise> promise = CreatePromise(mParent, aRv);
      if (!promise) {
        return nullptr;
      }
      promise->MaybeRejectWithTypeError<MSG_INVALID_ENUM_VALUE>(
          "mediation", "conditional", "CredentialMediationRequirement");
      return promise.forget();
    }

    EnsureWebAuthnHandler();
    return mWebAuthnHandler->GetAssertion(aOptions.mPublicKey.Value(),
                                          conditionallyMediated,
                                          aOptions.mSignal, aRv);
  }

  if (aOptions.mIdentity.WasPassed() &&
      StaticPrefs::dom_security_credentialmanagement_identity_enabled()) {
    RefPtr<Promise> promise = CreatePromise(mParent, aRv);
    if (!promise) {
      return nullptr;
    }

    if (conditionallyMediated) {
      promise->MaybeRejectWithTypeError<MSG_INVALID_ENUM_VALUE>(
          "mediation", "conditional", "CredentialMediationRequirement");
      return promise.forget();
    }

    WebIdentityHandler* identityHandler =
        mParent->GetOrCreateWebIdentityHandler();
    if (!identityHandler) {
      promise->MaybeRejectWithOperationError("");
      return promise.forget();
    }
    if (aOptions.mSignal.WasPassed()) {
      identityHandler->Follow(&aOptions.mSignal.Value());
    }
    identityHandler->GetCredential(aOptions, IsSameOriginWithAncestors(mParent),
                                   promise);

    return promise.forget();
  }

  return CreateAndRejectWithNotSupported(mParent, aRv);
}

already_AddRefed<Promise> CredentialsContainer::Create(
    const CredentialCreationOptions& aOptions, ErrorResult& aRv) {
  // Count the types of options provided. Must not be >1.
  uint64_t totalOptions = 0;
  if (aOptions.mPublicKey.WasPassed() &&
      StaticPrefs::security_webauth_webauthn()) {
    totalOptions += 1;
  }
  if (totalOptions > 1) {
    return CreateAndRejectWithNotSupported(mParent, aRv);
  }

  if (aOptions.mPublicKey.WasPassed() &&
      StaticPrefs::security_webauth_webauthn()) {
    MOZ_ASSERT(mParent);
    // In a cross-origin iframe this request consumes user activation, i.e.
    // subsequent requests cannot be made without further user interaction.
    // See step 2.2 of https://w3c.github.io/webauthn/#sctn-createCredential
    bool hasRequiredActivation =
        IsInActiveTab(mParent) &&
        (IsSameOriginWithAncestors(mParent) || ConsumeUserActivation(mParent));
    if (!FeaturePolicyUtils::IsFeatureAllowed(
            mParent->GetExtantDoc(), u"publickey-credentials-create"_ns) ||
        !hasRequiredActivation) {
      return CreateAndRejectWithNotAllowed(mParent, aRv);
    }

    EnsureWebAuthnHandler();
    return mWebAuthnHandler->MakeCredential(aOptions.mPublicKey.Value(),
                                            aOptions.mSignal, aRv);
  }

  return CreateAndRejectWithNotSupported(mParent, aRv);
}

already_AddRefed<Promise> CredentialsContainer::Store(
    const Credential& aCredential, ErrorResult& aRv) {
  nsString type;
  aCredential.GetType(type);
  if (type.EqualsLiteral("public-key") &&
      StaticPrefs::security_webauth_webauthn()) {
    if (!IsSameOriginWithAncestors(mParent) || !IsInActiveTab(mParent)) {
      return CreateAndRejectWithNotAllowed(mParent, aRv);
    }

    EnsureWebAuthnHandler();
    return mWebAuthnHandler->Store(aCredential, aRv);
  }

  return CreateAndRejectWithNotSupported(mParent, aRv);
}

already_AddRefed<Promise> CredentialsContainer::PreventSilentAccess(
    ErrorResult& aRv) {
  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(mParent);
  if (NS_WARN_IF(!global)) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  RefPtr<Promise> promise = Promise::Create(global, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  WebIdentityHandler* identityHandler =
      mParent->GetOrCreateWebIdentityHandler();
  if (!identityHandler) {
    promise->MaybeRejectWithOperationError("");
    return promise.forget();
  }

  identityHandler->PreventSilentAccess(promise);
  return promise.forget();
}

}  // namespace mozilla::dom
