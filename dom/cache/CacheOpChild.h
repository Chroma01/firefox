/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_cache_CacheOpChild_h
#define mozilla_dom_cache_CacheOpChild_h

#include "mozilla/dom/cache/ActorChild.h"
#include "mozilla/dom/cache/PCacheOpChild.h"
#include "mozilla/dom/cache/TypeUtils.h"
#include "mozilla/RefPtr.h"

class nsIGlobalObject;

namespace mozilla::dom {

class Promise;

namespace cache {

class CacheOpChild final : public PCacheOpChild,
                           public CacheActorChild,
                           public TypeUtils {
  friend class CacheChild;
  friend class CacheStorageChild;
  friend class PCacheOpChild;

 public:
  NS_INLINE_DECL_REFCOUNTING(CacheOpChild, override)

 private:
  // This class must be constructed by CacheChild or CacheStorageChild using
  // their ExecuteOp() factory method.
  CacheOpChild(SafeRefPtr<CacheWorkerRef> aWorkerRef, nsIGlobalObject* aGlobal,
               nsISupports* aParent, Promise* aPromise,
               ActorChild* aParentActor);
  ~CacheOpChild();

  // PCacheOpChild methods
  virtual void ActorDestroy(ActorDestroyReason aReason) override;

  mozilla::ipc::IPCResult Recv__delete__(ErrorResult&& aRv,
                                         const CacheOpResult& aResult);

  // ActorChild methods
  virtual void StartDestroy() override;

  // TypeUtils methods
  virtual nsIGlobalObject* GetGlobalObject() const override;

#ifdef DEBUG
  virtual void AssertOwningThread() const override;
#endif

  // Utility methods
  void HandleResponse(const Maybe<CacheResponse>& aMaybeResponse);

  void HandleResponseList(const nsTArray<CacheResponse>& aResponseList);

  void HandleRequestList(const nsTArray<CacheRequest>& aRequestList);

  nsCOMPtr<nsIGlobalObject> mGlobal;
  // Hold the parent Cache or CacheStorage object alive until this async
  // operation completes.
  nsCOMPtr<nsISupports> mParent;
  RefPtr<Promise> mPromise;
  ActorChild* mParentActor;
};

}  // namespace cache
}  // namespace mozilla::dom

#endif  // mozilla_dom_cache_CacheOpChild_h
