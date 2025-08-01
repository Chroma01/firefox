/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_cache_CacheOpParent_h
#define mozilla_dom_cache_CacheOpParent_h

#include "mozilla/InitializedOnce.h"
#include "mozilla/dom/cache/Manager.h"
#include "mozilla/dom/cache/PCacheOpParent.h"
#include "mozilla/dom/cache/PrincipalVerifier.h"
#include "nsTArray.h"

namespace mozilla {
namespace ipc {
class PBackgroundParent;
}  // namespace ipc
namespace dom::cache {

class CacheOpParent final : public PCacheOpParent,
                            public PrincipalVerifier::Listener,
                            public Manager::Listener {
  // to allow use of convenience overrides
  using Manager::Listener::OnOpComplete;

 public:
  CacheOpParent(const WeakRefParentType& aIpcManager,
                const CacheOpArgs& aOpArgs, CacheId aCacheId = INVALID_CACHE_ID,
                Namespace aNamespace = INVALID_NAMESPACE);

  void Execute(const SafeRefPtr<ManagerId>& aManagerId);

  void Execute(SafeRefPtr<cache::Manager> aManager);

  void WaitForVerification(PrincipalVerifier* aVerifier);

  NS_INLINE_DECL_REFCOUNTING(CacheOpParent, override)

 private:
  ~CacheOpParent();

  // PCacheOpParent methods
  virtual void ActorDestroy(ActorDestroyReason aReason) override;

  // PrincipalVerifier::Listener methods
  virtual void OnPrincipalVerified(
      nsresult aRv, const SafeRefPtr<ManagerId>& aManagerId) override;

  // Manager::Listener methods
  virtual void OnOpComplete(ErrorResult&& aRv, const CacheOpResult& aResult,
                            CacheId aOpenedCacheId,
                            const Maybe<StreamInfo>& aStreamInfo) override;

  // utility methods
  already_AddRefed<nsIInputStream> DeserializeCacheStream(
      const Maybe<CacheReadStream>& aMaybeStream);

  void ProcessCrossOriginResourcePolicyHeader(
      ErrorResult& aRv, const nsTArray<SavedResponse>& aResponses);

  mozilla::LazyInitializedOnceEarlyDestructible<const WeakRefParentType>
      mIpcManager;
  const CacheId mCacheId;
  const Namespace mNamespace;
  const CacheOpArgs mOpArgs;
  SafeRefPtr<cache::Manager> mManager;
  RefPtr<PrincipalVerifier> mVerifier;
};

}  // namespace dom::cache
}  // namespace mozilla

#endif  // mozilla_dom_cache_CacheOpParent_h
