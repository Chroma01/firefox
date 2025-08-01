/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_CSSPropertyRule_h
#define mozilla_dom_CSSPropertyRule_h

#include "mozilla/ServoBindingTypes.h"
#include "mozilla/css/Rule.h"
#include "nsICSSDeclaration.h"

struct StylePropertyRule;

namespace mozilla::dom {

class CSSPropertyRule final : public css::Rule {
 public:
  CSSPropertyRule(already_AddRefed<StylePropertyRule> aRawRule,
                  StyleSheet* aSheet, css::Rule* aParentRule, uint32_t aLine,
                  uint32_t aColumn)
      : css::Rule(aSheet, aParentRule, aLine, aColumn),
        mRawRule(std::move(aRawRule)) {}

  bool IsCCLeaf() const final;

  StylePropertyRule* Raw() const { return mRawRule; }
  void SetRawAfterClone(RefPtr<StylePropertyRule> aRaw);

  JSObject* WrapObject(JSContext* aCx, JS::Handle<JSObject*> aGivenProto) final;

#ifdef DEBUG
  void List(FILE* out = stdout, int32_t aIndent = 0) const final;
#endif

  size_t SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) const final;

  // WebIDL interfaces
  StyleCssRuleType Type() const final;

  void GetName(nsACString& aName) const;

  void GetSyntax(nsACString& aSyntax) const;

  bool Inherits() const;

  void GetInitialValue(nsACString& aInitialValueStr) const;

  void GetCssText(nsACString& aCssText) const final;

 private:
  ~CSSPropertyRule() = default;

  RefPtr<StylePropertyRule> mRawRule;
};

}  // namespace mozilla::dom

#endif  // mozilla_dom_CSSPropertyRule_h
