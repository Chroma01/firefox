/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/HTMLDetailsElement.h"

#include "mozilla/dom/HTMLDetailsElementBinding.h"
#include "mozilla/dom/HTMLSummaryElement.h"
#include "mozilla/dom/ShadowRoot.h"
#include "mozilla/BuiltInStyleSheets.h"
#include "mozilla/StaticPrefs_dom.h"
#include "nsContentUtils.h"
#include "nsTextNode.h"

NS_IMPL_NS_NEW_HTML_ELEMENT(Details)

namespace mozilla::dom {

HTMLDetailsElement::~HTMLDetailsElement() = default;

NS_IMPL_ELEMENT_CLONE(HTMLDetailsElement)

HTMLDetailsElement::HTMLDetailsElement(already_AddRefed<NodeInfo>&& aNodeInfo)
    : nsGenericHTMLElement(std::move(aNodeInfo)) {
  SetupShadowTree();
}

HTMLSummaryElement* HTMLDetailsElement::GetFirstSummary() const {
  // XXX: Bug 1245032: Might want to cache the first summary element.
  for (nsIContent* child = nsINode::GetFirstChild(); child;
       child = child->GetNextSibling()) {
    if (auto* summary = HTMLSummaryElement::FromNode(child)) {
      return summary;
    }
  }
  return nullptr;
}

void HTMLDetailsElement::AfterSetAttr(int32_t aNameSpaceID, nsAtom* aName,
                                      const nsAttrValue* aValue,
                                      const nsAttrValue* aOldValue,
                                      nsIPrincipal* aMaybeScriptedPrincipal,
                                      bool aNotify) {
  if (aNameSpaceID == kNameSpaceID_None) {
    if (aName == nsGkAtoms::open) {
      bool wasOpen = !!aOldValue;
      bool isOpen = !!aValue;
      if (wasOpen != isOpen) {
        auto stringForState = [](bool aOpen) {
          return aOpen ? u"open"_ns : u"closed"_ns;
        };
        nsAutoString oldState;
        if (mToggleEventDispatcher) {
          oldState.Truncate();
          static_cast<ToggleEvent*>(mToggleEventDispatcher->mEvent.get())
              ->GetOldState(oldState);
          mToggleEventDispatcher->Cancel();
        } else {
          oldState.Assign(stringForState(wasOpen));
        }
        RefPtr<ToggleEvent> toggleEvent = CreateToggleEvent(
            u"toggle"_ns, oldState, stringForState(isOpen), Cancelable::eNo);
        mToggleEventDispatcher =
            new AsyncEventDispatcher(this, toggleEvent.forget());
        mToggleEventDispatcher->PostDOMEvent();

        if (isOpen) {
          CloseOtherElementsIfNeeded();
        }
        SetStates(ElementState::OPEN, isOpen);
      }
    } else if (aName == nsGkAtoms::name) {
      CloseElementIfNeeded();
    }
  }

  return nsGenericHTMLElement::AfterSetAttr(
      aNameSpaceID, aName, aValue, aOldValue, aMaybeScriptedPrincipal, aNotify);
}

nsresult HTMLDetailsElement::BindToTree(BindContext& aContext,
                                        nsINode& aParent) {
  nsresult rv = nsGenericHTMLElement::BindToTree(aContext, aParent);
  NS_ENSURE_SUCCESS(rv, rv);

  CloseElementIfNeeded();

  return NS_OK;
}

void HTMLDetailsElement::SetupShadowTree() {
  const bool kNotify = false;
  AttachAndSetUAShadowRoot(NotifyUAWidgetSetup::No);
  RefPtr<ShadowRoot> sr = GetShadowRoot();
  if (NS_WARN_IF(!sr)) {
    return;
  }

  nsNodeInfoManager* nim = OwnerDoc()->NodeInfoManager();
  RefPtr<NodeInfo> slotNodeInfo = nim->GetNodeInfo(
      nsGkAtoms::slot, nullptr, kNameSpaceID_XHTML, nsINode::ELEMENT_NODE);
  sr->AppendBuiltInStyleSheet(BuiltInStyleSheet::Details);
  {
    RefPtr<nsGenericHTMLElement> slot =
        NS_NewHTMLSlotElement(do_AddRef(slotNodeInfo));
    if (NS_WARN_IF(!slot)) {
      return;
    }
    slot->SetAttr(kNameSpaceID_None, nsGkAtoms::name,
                  u"internal-main-summary"_ns, kNotify);
    sr->AppendChildTo(slot, kNotify, IgnoreErrors());

    RefPtr<NodeInfo> summaryNodeInfo = nim->GetNodeInfo(
        nsGkAtoms::summary, nullptr, kNameSpaceID_XHTML, nsINode::ELEMENT_NODE);
    RefPtr<nsGenericHTMLElement> summary =
        NS_NewHTMLSummaryElement(summaryNodeInfo.forget());
    if (NS_WARN_IF(!summary)) {
      return;
    }

    nsAutoString defaultSummaryText;
    nsContentUtils::GetMaybeLocalizedString(nsContentUtils::eFORMS_PROPERTIES,
                                            "DefaultSummary", OwnerDoc(),
                                            defaultSummaryText);
    RefPtr<nsTextNode> description = new (nim) nsTextNode(nim);
    description->SetText(defaultSummaryText, kNotify);
    summary->AppendChildTo(description, kNotify, IgnoreErrors());

    slot->AppendChildTo(summary, kNotify, IgnoreErrors());
  }
  {
    RefPtr<nsGenericHTMLElement> slot =
        NS_NewHTMLSlotElement(slotNodeInfo.forget());
    if (NS_WARN_IF(!slot)) {
      return;
    }
    if (StaticPrefs::layout_css_details_content_enabled()) {
      slot->SetPseudoElementType(PseudoStyleType::detailsContent);
    }
    sr->AppendChildTo(slot, kNotify, IgnoreErrors());
  }
}

void HTMLDetailsElement::AsyncEventRunning(AsyncEventDispatcher* aEvent) {
  if (mToggleEventDispatcher == aEvent) {
    mToggleEventDispatcher = nullptr;
  }
}

JSObject* HTMLDetailsElement::WrapNode(JSContext* aCx,
                                       JS::Handle<JSObject*> aGivenProto) {
  return HTMLDetailsElement_Binding::Wrap(aCx, this, aGivenProto);
}

bool HTMLDetailsElement::IsValidCommandAction(Command aCommand) const {
  return nsGenericHTMLElement::IsValidCommandAction(aCommand) ||
         (StaticPrefs::dom_element_commandfor_on_details_enabled() &&
          (aCommand == Command::Toggle || aCommand == Command::Close ||
           aCommand == Command::Open));
}

bool HTMLDetailsElement::HandleCommandInternal(Element* aSource,
                                               Command aCommand,
                                               ErrorResult& aRv) {
  if (nsGenericHTMLElement::HandleCommandInternal(aSource, aCommand, aRv)) {
    return true;
  }

  MOZ_ASSERT(StaticPrefs::dom_element_commandfor_on_details_enabled());
  if (aCommand == Command::Toggle) {
    ToggleOpen();
    return true;
  }
  if (aCommand == Command::Close) {
    if (Open()) {
      SetOpen(false, IgnoreErrors());
    }
    return true;
  }
  if (aCommand == Command::Open) {
    if (!Open()) {
      SetOpen(true, IgnoreErrors());
    }
    return true;
  }

  return false;
}

void HTMLDetailsElement::CloseElementIfNeeded() {
  if (!StaticPrefs::dom_details_group_enabled()) {
    return;
  }

  if (!Open()) {
    return;
  }

  if (!HasName()) {
    return;
  }

  RefPtr<nsAtom> name = GetParsedAttr(nsGkAtoms::name)->GetAsAtom();

  RefPtr<Document> doc = OwnerDoc();
  bool oldFlag = doc->FireMutationEvents();
  doc->SetFireMutationEvents(false);

  nsINode* root = SubtreeRoot();
  for (nsINode* cur = root; cur; cur = cur->GetNextNode(root)) {
    if (!cur->HasName()) {
      continue;
    }
    if (auto* other = HTMLDetailsElement::FromNode(cur)) {
      if (other != this && other->Open() &&
          other->AttrValueIs(kNameSpaceID_None, nsGkAtoms::name, name,
                             eCaseMatters)) {
        SetOpen(false, IgnoreErrors());
        break;
      }
    }
  }

  doc->SetFireMutationEvents(oldFlag);
}

void HTMLDetailsElement::CloseOtherElementsIfNeeded() {
  if (!StaticPrefs::dom_details_group_enabled()) {
    return;
  }

  MOZ_ASSERT(Open());

  if (!HasName()) {
    return;
  }

  RefPtr<nsAtom> name = GetParsedAttr(nsGkAtoms::name)->GetAsAtom();

  RefPtr<Document> doc = OwnerDoc();
  bool oldFlag = doc->FireMutationEvents();
  doc->SetFireMutationEvents(false);

  nsINode* root = SubtreeRoot();
  for (nsINode* cur = root; cur; cur = cur->GetNextNode(root)) {
    if (!cur->HasName()) {
      continue;
    }
    if (auto* other = HTMLDetailsElement::FromNode(cur)) {
      if (other != this && other->Open() &&
          other->AttrValueIs(kNameSpaceID_None, nsGkAtoms::name, name,
                             eCaseMatters)) {
        RefPtr<HTMLDetailsElement> otherDetails = other;
        otherDetails->SetOpen(false, IgnoreErrors());
        break;
      }
    }
  }

  doc->SetFireMutationEvents(oldFlag);
}

}  // namespace mozilla::dom
