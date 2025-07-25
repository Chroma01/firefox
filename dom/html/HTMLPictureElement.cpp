/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/HTMLPictureElement.h"
#include "mozilla/dom/HTMLPictureElementBinding.h"
#include "mozilla/dom/HTMLImageElement.h"
#include "mozilla/dom/HTMLSourceElement.h"

// Expand NS_IMPL_NS_NEW_HTML_ELEMENT(Picture) to add pref check.
nsGenericHTMLElement* NS_NewHTMLPictureElement(
    already_AddRefed<mozilla::dom::NodeInfo>&& aNodeInfo,
    mozilla::dom::FromParser aFromParser) {
  RefPtr<mozilla::dom::NodeInfo> nodeInfo(aNodeInfo);
  auto* nim = nodeInfo->NodeInfoManager();
  return new (nim) mozilla::dom::HTMLPictureElement(nodeInfo.forget());
}

namespace mozilla::dom {

HTMLPictureElement::HTMLPictureElement(
    already_AddRefed<mozilla::dom::NodeInfo>&& aNodeInfo)
    : nsGenericHTMLElement(std::move(aNodeInfo)) {}

HTMLPictureElement::~HTMLPictureElement() = default;

NS_IMPL_ELEMENT_CLONE(HTMLPictureElement)

void HTMLPictureElement::RemoveChildNode(nsIContent* aKid, bool aNotify,
                                         const BatchRemovalState* aState,
                                         nsINode* aNewParent) {
  MOZ_ASSERT(aKid);

  if (auto* img = HTMLImageElement::FromNode(aKid)) {
    img->PictureSourceRemoved(aNotify);
  } else if (auto* source = HTMLSourceElement::FromNode(aKid)) {
    // Find all img siblings after this <source> to notify them of its demise
    nsCOMPtr<nsIContent> nextSibling = source->GetNextSibling();
    if (nextSibling && nextSibling->GetParentNode() == this) {
      do {
        if (auto* img = HTMLImageElement::FromNode(nextSibling)) {
          img->PictureSourceRemoved(aNotify, source);
        }
      } while ((nextSibling = nextSibling->GetNextSibling()));
    }
  }

  nsGenericHTMLElement::RemoveChildNode(aKid, aNotify, aState, aNewParent);
}

void HTMLPictureElement::InsertChildBefore(nsIContent* aKid,
                                           nsIContent* aBeforeThis,
                                           bool aNotify, ErrorResult& aRv,
                                           nsINode* aOldParent) {
  nsGenericHTMLElement::InsertChildBefore(aKid, aBeforeThis, aNotify, aRv,
                                          aOldParent);
  if (aRv.Failed() || !aKid) {
    return;
  }

  if (auto* img = HTMLImageElement::FromNode(aKid)) {
    img->PictureSourceAdded(aNotify);
  } else if (auto* source = HTMLSourceElement::FromNode(aKid)) {
    // Find all img siblings after this <source> to notify them of its insertion
    nsCOMPtr<nsIContent> nextSibling = source->GetNextSibling();
    if (nextSibling && nextSibling->GetParentNode() == this) {
      do {
        if (auto* img = HTMLImageElement::FromNode(nextSibling)) {
          img->PictureSourceAdded(aNotify, source);
        }
      } while ((nextSibling = nextSibling->GetNextSibling()));
    }
  }
}

JSObject* HTMLPictureElement::WrapNode(JSContext* aCx,
                                       JS::Handle<JSObject*> aGivenProto) {
  return HTMLPictureElement_Binding::Wrap(aCx, this, aGivenProto);
}

}  // namespace mozilla::dom
