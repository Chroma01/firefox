/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* atom list for CSS pseudo-elements */

#ifndef nsCSSPseudoElements_h___
#define nsCSSPseudoElements_h___

#include "mozilla/CSSEnabledState.h"
#include "mozilla/PseudoStyleType.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/StaticPrefs_layout.h"
#include "nsGkAtoms.h"

// Is this pseudo-element a CSS2 pseudo-element that can be specified
// with the single colon syntax (in addition to the double-colon syntax,
// which can be used for all pseudo-elements)?
//
// Note: We also rely on this for IsEagerlyCascadedInServo.
#define CSS_PSEUDO_ELEMENT_IS_CSS2 (1 << 0)
// Flag that indicate the pseudo-element supports a user action pseudo-class
// following it, such as :active or :hover.  This would normally correspond
// to whether the pseudo-element is tree-like, but we don't support these
// pseudo-classes on ::before and ::after generated content yet.  See
// http://dev.w3.org/csswg/selectors4/#pseudo-elements.
#define CSS_PSEUDO_ELEMENT_SUPPORTS_USER_ACTION_STATE (1 << 3)
// Should this pseudo-element be enabled only for UA sheets?
#define CSS_PSEUDO_ELEMENT_ENABLED_IN_UA_SHEETS (1 << 4)
// Should this pseudo-element be enabled only for UA sheets and chrome
// stylesheets?
#define CSS_PSEUDO_ELEMENT_ENABLED_IN_CHROME (1 << 5)

#define CSS_PSEUDO_ELEMENT_ENABLED_IN_UA_SHEETS_AND_CHROME \
  (CSS_PSEUDO_ELEMENT_ENABLED_IN_UA_SHEETS |               \
   CSS_PSEUDO_ELEMENT_ENABLED_IN_CHROME)

// Can we use the ChromeOnly document.createElement(..., { pseudo: "::foo" })
// API for creating pseudo-implementing native anonymous content in JS with this
// pseudo-element?
#define CSS_PSEUDO_ELEMENT_IS_JS_CREATED_NAC (1 << 6)
// Does this pseudo-element act like an item for containers (such as flex and
// grid containers) and thus needs parent display-based style fixup?
#define CSS_PSEUDO_ELEMENT_IS_FLEX_OR_GRID_ITEM (1 << 7)

class nsCSSPseudoElements {
  using EnabledState = mozilla::CSSEnabledState;
  using Request = mozilla::PseudoStyleRequest;
  using Type = mozilla::PseudoStyleType;

 public:
  static bool IsEagerlyCascadedInServo(const Type aType) {
    return PseudoElementHasFlags(aType, CSS_PSEUDO_ELEMENT_IS_CSS2);
  }

 public:
#ifdef DEBUG
  static void AssertAtoms();
#endif

// Alias nsCSSPseudoElements::foo() to nsGkAtoms::foo.
#define CSS_PSEUDO_ELEMENT(name_, value_, flags_)         \
  static nsCSSPseudoElementStaticAtom* name_() {          \
    return const_cast<nsCSSPseudoElementStaticAtom*>(     \
        static_cast<const nsCSSPseudoElementStaticAtom*>( \
            nsGkAtoms::PseudoElement_##name_));           \
  }
#include "nsCSSPseudoElementList.h"
#undef CSS_PSEUDO_ELEMENT

  // Returns an empty Request for a syntactically invalid pseudo-element, and
  // NotPseudo for the empty / null string.
  static mozilla::Maybe<Request> ParsePseudoElement(
      const nsAString& aPseudoElement,
      EnabledState = EnabledState::ForAllContent);

  // Get the atom for a given Type. aType must be <
  // PseudoType::CSSPseudoElementsEnd.
  // This only ever returns static atoms, so it's fine to return a raw pointer.
  static nsAtom* GetPseudoAtom(Type aType);

  static bool PseudoElementSupportsUserActionState(const Type aType);

  static bool PseudoElementIsJSCreatedNAC(Type aType) {
    return PseudoElementHasFlags(aType, CSS_PSEUDO_ELEMENT_IS_JS_CREATED_NAC);
  }

  static bool PseudoElementIsFlexOrGridItem(const Type aType) {
    return PseudoElementHasFlags(aType,
                                 CSS_PSEUDO_ELEMENT_IS_FLEX_OR_GRID_ITEM);
  }

  static bool EnabledInContent(Type aType) {
    switch (aType) {
      case Type::highlight:
        return mozilla::StaticPrefs::dom_customHighlightAPI_enabled();
      case Type::targetText:
        return mozilla::StaticPrefs::dom_text_fragments_enabled();
      case Type::sliderTrack:
      case Type::sliderThumb:
      case Type::sliderFill:
        return mozilla::StaticPrefs::layout_css_modern_range_pseudos_enabled();
      case Type::detailsContent:
        return mozilla::StaticPrefs::layout_css_details_content_enabled();
      case Type::viewTransition:
      case Type::viewTransitionGroup:
      case Type::viewTransitionImagePair:
      case Type::viewTransitionOld:
      case Type::viewTransitionNew:
        return mozilla::StaticPrefs::dom_viewTransitions_enabled();
      default:
        return !PseudoElementHasAnyFlag(
            aType, CSS_PSEUDO_ELEMENT_ENABLED_IN_UA_SHEETS_AND_CHROME);
    }
  }

  static bool IsEnabled(Type aType, EnabledState aEnabledState) {
    if (EnabledInContent(aType)) {
      return true;
    }

    if ((aEnabledState & EnabledState::InUASheets) &&
        PseudoElementHasFlags(aType, CSS_PSEUDO_ELEMENT_ENABLED_IN_UA_SHEETS)) {
      return true;
    }

    if ((aEnabledState & EnabledState::InChrome) &&
        PseudoElementHasFlags(aType, CSS_PSEUDO_ELEMENT_ENABLED_IN_CHROME)) {
      return true;
    }

    return false;
  }

  static nsString PseudoRequestAsString(const Request& aPseudoRequest);

 private:
  // Does the given pseudo-element have all of the flags given?
  static bool PseudoElementHasFlags(const Type aType, uint32_t aFlags) {
    MOZ_ASSERT(aType < Type::CSSPseudoElementsEnd);
    return (kPseudoElementFlags[size_t(aType)] & aFlags) == aFlags;
  }

  static bool PseudoElementHasAnyFlag(const Type aType, uint32_t aFlags) {
    MOZ_ASSERT(aType < Type::CSSPseudoElementsEnd);
    return (kPseudoElementFlags[size_t(aType)] & aFlags) != 0;
  }

  static nsStaticAtom* GetAtomBase();

  static const uint32_t kPseudoElementFlags[size_t(Type::CSSPseudoElementsEnd)];
};

#endif /* nsCSSPseudoElements_h___ */
