/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_SVG_SVGELEMENT_H_
#define DOM_SVG_SVGELEMENT_H_

/*
  SVGElement is the base class for all SVG content elements.
  It implements all the common DOM interfaces and handles attributes.
*/

#include "mozilla/Attributes.h"
#include "mozilla/RefPtr.h"
#include "mozilla/SVGAnimatedClass.h"
#include "mozilla/SVGContentUtils.h"
#include "mozilla/dom/DOMRect.h"
#include "mozilla/dom/Element.h"
#include "mozilla/gfx/MatrixFwd.h"
#include "mozilla/UniquePtr.h"
#include "nsCSSPropertyID.h"
#include "nsChangeHint.h"
#include "nsCycleCollectionParticipant.h"
#include "nsError.h"
#include "nsISupportsImpl.h"
#include "nsStyledElement.h"
#include "gfxMatrix.h"

// {70db954d-e452-4be3-83aa-f54a51cf7890}
#define MOZILLA_SVGELEMENT_IID \
  {0x70db954d, 0xe452, 0x4be3, {0x82, 0xaa, 0xf5, 0x4a, 0x51, 0xcf, 0x78, 0x90}}

nsresult NS_NewSVGElement(mozilla::dom::Element** aResult,
                          already_AddRefed<mozilla::dom::NodeInfo>&& aNodeInfo);

class mozAutoDocUpdate;

namespace mozilla {

class SVGAnimatedBoolean;
class SVGAnimatedEnumeration;
class SVGAnimatedInteger;
class SVGAnimatedIntegerPair;
class SVGAnimatedLength;
class SVGAnimatedLengthList;
class SVGAnimatedNumber;
class SVGAnimatedNumberList;
class SVGAnimatedNumberPair;
class SVGAnimatedOrient;
class SVGAnimatedPathSegList;
class SVGAnimatedPointList;
class SVGAnimatedString;
class SVGAnimatedPreserveAspectRatio;
class SVGAnimatedTransformList;
class SVGAnimatedViewBox;
class SVGNumberList;
class SVGStringList;
class SVGUserUnitList;

struct SVGEnumMapping;

namespace dom {
class DOMSVGStringList;
class SVGSVGElement;
class SVGViewportElement;

using SVGElementBase = nsStyledElement;

class SVGElement : public SVGElementBase  // nsIContent
{
 protected:
  explicit SVGElement(already_AddRefed<mozilla::dom::NodeInfo>&& aNodeInfo);
  friend nsresult(
      ::NS_NewSVGElement(mozilla::dom::Element** aResult,
                         already_AddRefed<mozilla::dom::NodeInfo>&& aNodeInfo));
  nsresult Init();
  virtual ~SVGElement();

 public:
  nsresult Clone(mozilla::dom::NodeInfo*,
                 nsINode** aResult) const MOZ_MUST_OVERRIDE override;

  // From Element
  nsresult CopyInnerTo(mozilla::dom::Element* aDest);

  NS_INLINE_DECL_STATIC_IID(MOZILLA_SVGELEMENT_IID)
  // nsISupports
  NS_INLINE_DECL_REFCOUNTING_INHERITED(SVGElement, SVGElementBase)

  NS_DECL_ADDSIZEOFEXCLUDINGTHIS

  NS_IMETHOD QueryInterface(REFNSIID aIID, void** aInstancePtr) override;

  void DidAnimateClass();

  void SetNonce(const nsAString& aNonce) {
    SetProperty(nsGkAtoms::nonce, new nsString(aNonce),
                nsINode::DeleteProperty<nsString>, /* aTransfer = */ true);
  }
  void RemoveNonce() { RemoveProperty(nsGkAtoms::nonce); }
  void GetNonce(nsAString& aNonce) const {
    nsString* cspNonce = static_cast<nsString*>(GetProperty(nsGkAtoms::nonce));
    if (cspNonce) {
      aNonce = *cspNonce;
    }
  }

  // nsIContent interface methods

  nsresult BindToTree(BindContext&, nsINode& aParent) override;

  nsChangeHint GetAttributeChangeHint(const nsAtom* aAttribute,
                                      int32_t aModType) const override;

  /**
   * We override the default to unschedule computation of Servo declaration
   * blocks when adopted across documents.
   */
  void NodeInfoChanged(Document* aOldDoc) override;

  NS_IMETHOD_(bool) IsAttributeMapped(const nsAtom* aAttribute) const override;
  void UpdateMappedDeclarationBlock();

  NS_IMPL_FROMNODE(SVGElement, kNameSpaceID_SVG)

  // Gets the element that establishes the rectangular viewport against which
  // we should resolve percentage lengths (our "coordinate context"). Returns
  // nullptr for outer <svg> or SVG without an <svg> parent (invalid SVG).
  mozilla::dom::SVGViewportElement* GetCtx() const;

  /**
   * Returns the transforms from the coordinate space established by this
   * element for its children to this element's userspace. This includes any
   * offsets due to e.g. 'x'/'y' attributes, and any transform due to a
   * 'viewBox' attribute.
   */
  virtual gfxMatrix ChildToUserSpaceTransform() const;

  // Setter for to set the current <animateMotion> transformation
  // Only visible for SVGGraphicElement, so it's a no-op here, and that
  // subclass has the useful implementation.
  virtual void SetAnimateMotionTransform(
      const mozilla::gfx::Matrix* aMatrix) { /*no-op*/ }
  virtual const mozilla::gfx::Matrix* GetAnimateMotionTransform() const {
    return nullptr;
  }

  bool IsStringAnimatable(uint8_t aAttrEnum) {
    return GetStringInfo().mInfos[aAttrEnum].mIsAnimatable;
  }
  bool NumberAttrAllowsPercentage(uint8_t aAttrEnum) {
    return IsSVGElement(nsGkAtoms::stop) &&
           GetNumberInfo().mInfos[aAttrEnum].mName == nsGkAtoms::offset;
  }
  virtual bool HasValidDimensions() const { return true; }
  void SetLength(nsAtom* aName, const SVGAnimatedLength& aLength);

  enum class ValToUse { Base, Anim };
  static bool UpdateDeclarationBlockFromLength(
      const StyleLockedDeclarationBlock&, nsCSSPropertyID,
      const SVGAnimatedLength&, ValToUse);
  static bool UpdateDeclarationBlockFromPath(const StyleLockedDeclarationBlock&,
                                             const SVGAnimatedPathSegList&,
                                             ValToUse);
  static bool UpdateDeclarationBlockFromTransform(
      const StyleLockedDeclarationBlock&, const SVGAnimatedTransformList*,
      const gfx::Matrix* aAnimateMotionTransform, ValToUse);

  nsAttrValue WillChangeLength(uint8_t aAttrEnum,
                               const mozAutoDocUpdate& aProofOfUpdate);
  nsAttrValue WillChangeNumberPair(uint8_t aAttrEnum);
  nsAttrValue WillChangeIntegerPair(uint8_t aAttrEnum,
                                    const mozAutoDocUpdate& aProofOfUpdate);
  nsAttrValue WillChangeOrient(const mozAutoDocUpdate& aProofOfUpdate);
  nsAttrValue WillChangeViewBox(const mozAutoDocUpdate& aProofOfUpdate);
  nsAttrValue WillChangePreserveAspectRatio(
      const mozAutoDocUpdate& aProofOfUpdate);
  nsAttrValue WillChangeNumberList(uint8_t aAttrEnum,
                                   const mozAutoDocUpdate& aProofOfUpdate);
  nsAttrValue WillChangeLengthList(uint8_t aAttrEnum,
                                   const mozAutoDocUpdate& aProofOfUpdate);
  nsAttrValue WillChangePointList(const mozAutoDocUpdate& aProofOfUpdate);
  nsAttrValue WillChangePathSegList(const mozAutoDocUpdate& aProofOfUpdate);
  nsAttrValue WillChangeTransformList(const mozAutoDocUpdate& aProofOfUpdate);
  nsAttrValue WillChangeStringList(bool aIsConditionalProcessingAttribute,
                                   uint8_t aAttrEnum,
                                   const mozAutoDocUpdate& aProofOfUpdate);

  void DidChangeLength(uint8_t aAttrEnum, const nsAttrValue& aEmptyOrOldValue,
                       const mozAutoDocUpdate& aProofOfUpdate);
  void DidChangeNumber(uint8_t aAttrEnum);
  void DidChangeNumberPair(uint8_t aAttrEnum,
                           const nsAttrValue& aEmptyOrOldValue);
  void DidChangeInteger(uint8_t aAttrEnum);
  void DidChangeIntegerPair(uint8_t aAttrEnum,
                            const nsAttrValue& aEmptyOrOldValue,
                            const mozAutoDocUpdate& aProofOfUpdate);
  void DidChangeBoolean(uint8_t aAttrEnum);
  void DidChangeEnum(uint8_t aAttrEnum);
  void DidChangeOrient(const nsAttrValue& aEmptyOrOldValue,
                       const mozAutoDocUpdate& aProofOfUpdate);
  void DidChangeViewBox(const nsAttrValue& aEmptyOrOldValue,
                        const mozAutoDocUpdate& aProofOfUpdate);
  void DidChangePreserveAspectRatio(const nsAttrValue& aEmptyOrOldValue,
                                    const mozAutoDocUpdate& aProofOfUpdate);
  void DidChangeNumberList(uint8_t aAttrEnum,
                           const nsAttrValue& aEmptyOrOldValue,
                           const mozAutoDocUpdate& aProofOfUpdate);
  void DidChangeLengthList(uint8_t aAttrEnum,
                           const nsAttrValue& aEmptyOrOldValue,
                           const mozAutoDocUpdate& aProofOfUpdate);
  void DidChangePointList(const nsAttrValue& aEmptyOrOldValue,
                          const mozAutoDocUpdate& aProofOfUpdate);
  void DidChangePathSegList(const nsAttrValue& aEmptyOrOldValue,
                            const mozAutoDocUpdate& aProofOfUpdate);
  void DidChangeTransformList(const nsAttrValue& aEmptyOrOldValue,
                              const mozAutoDocUpdate& aProofOfUpdate);
  void DidChangeString(uint8_t aAttrEnum) {}
  void DidChangeStringList(bool aIsConditionalProcessingAttribute,
                           uint8_t aAttrEnum,
                           const nsAttrValue& aEmptyOrOldValue,
                           const mozAutoDocUpdate& aProofOfUpdate);

  void DidAnimateLength(uint8_t aAttrEnum);
  void DidAnimateNumber(uint8_t aAttrEnum) {
    auto info = GetNumberInfo();
    DidAnimateAttribute(kNameSpaceID_None, info.mInfos[aAttrEnum].mName);
  }
  void DidAnimateNumberPair(uint8_t aAttrEnum) {
    auto info = GetNumberPairInfo();
    DidAnimateAttribute(kNameSpaceID_None, info.mInfos[aAttrEnum].mName);
  }
  void DidAnimateInteger(uint8_t aAttrEnum) {
    auto info = GetIntegerInfo();
    DidAnimateAttribute(kNameSpaceID_None, info.mInfos[aAttrEnum].mName);
  }
  void DidAnimateIntegerPair(uint8_t aAttrEnum) {
    auto info = GetIntegerPairInfo();
    DidAnimateAttribute(kNameSpaceID_None, info.mInfos[aAttrEnum].mName);
  }
  void DidAnimateBoolean(uint8_t aAttrEnum) {
    auto info = GetBooleanInfo();
    DidAnimateAttribute(kNameSpaceID_None, info.mInfos[aAttrEnum].mName);
  }
  void DidAnimateEnum(uint8_t aAttrEnum) {
    auto info = GetEnumInfo();
    DidAnimateAttribute(kNameSpaceID_None, info.mInfos[aAttrEnum].mName);
  }
  void DidAnimateOrient() {
    DidAnimateAttribute(kNameSpaceID_None, nsGkAtoms::orient);
  }
  void DidAnimateViewBox() {
    DidAnimateAttribute(kNameSpaceID_None, nsGkAtoms::viewBox);
  }
  void DidAnimatePreserveAspectRatio() {
    DidAnimateAttribute(kNameSpaceID_None, nsGkAtoms::preserveAspectRatio);
  }
  void DidAnimateNumberList(uint8_t aAttrEnum) {
    auto info = GetNumberListInfo();
    DidAnimateAttribute(kNameSpaceID_None, info.mInfos[aAttrEnum].mName);
  }
  void DidAnimateLengthList(uint8_t aAttrEnum) {
    auto info = GetLengthListInfo();
    DidAnimateAttribute(kNameSpaceID_None, info.mInfos[aAttrEnum].mName);
  }
  void DidAnimatePointList();
  void DidAnimatePathSegList();
  void DidAnimateTransformList(int32_t aModType);
  void DidAnimateString(uint8_t aAttrEnum) {
    auto info = GetStringInfo();
    DidAnimateAttribute(info.mInfos[aAttrEnum].mNamespaceID,
                        info.mInfos[aAttrEnum].mName);
  }

  enum {
    /**
     * Flag to indicate to GetAnimatedXxx() methods that the object being
     * requested should be allocated if it hasn't already been allocated, and
     * that the method should not return null. Only applicable to methods that
     * need to allocate the object that they return.
     */
    DO_ALLOCATE = 0x1
  };

  SVGAnimatedLength* GetAnimatedLength(uint8_t aAttrEnum);
  SVGAnimatedLength* GetAnimatedLength(const nsAtom* aAttrName);
  void GetAnimatedLengthValues(float* aFirst, ...);
  void GetAnimatedNumberValues(float* aFirst, ...);
  void GetAnimatedIntegerValues(int32_t* aFirst, ...);
  SVGAnimatedNumberList* GetAnimatedNumberList(uint8_t aAttrEnum);
  SVGAnimatedNumberList* GetAnimatedNumberList(nsAtom* aAttrName);
  void GetAnimatedLengthListValues(SVGUserUnitList* aFirst, ...);
  SVGAnimatedLengthList* GetAnimatedLengthList(uint8_t aAttrEnum);
  virtual SVGAnimatedPointList* GetAnimatedPointList() { return nullptr; }
  virtual SVGAnimatedPathSegList* GetAnimPathSegList() {
    // DOM interface 'SVGAnimatedPathData' (*inherited* by SVGPathElement)
    // has a member called 'animatedPathSegList' member, so we have a shorter
    // name so we don't get hidden by the GetAnimatedPathSegList declared by
    // NS_DECL_NSIDOMSVGANIMATEDPATHDATA.
    return nullptr;
  }
  /**
   * Get the SVGAnimatedTransformList for this element.
   *
   * Despite the fact that animated transform lists are used for a variety of
   * attributes, no SVG element uses more than one.
   *
   * It's relatively uncommon for elements to have their transform attribute
   * set, so to save memory the SVGAnimatedTransformList is not allocated
   * until the attribute is set/animated or its DOM wrapper is created. Callers
   * that require the SVGAnimatedTransformList to be allocated and for this
   * method to return non-null must pass the DO_ALLOCATE flag.
   */
  virtual SVGAnimatedTransformList* GetAnimatedTransformList(
      uint32_t aFlags = 0) {
    return nullptr;
  }

  mozilla::UniquePtr<SMILAttr> GetAnimatedAttr(int32_t aNamespaceID,
                                               nsAtom* aName) override;
  void AnimationNeedsResample();
  void FlushAnimations();

  void GetStringBaseValue(uint8_t aAttrEnum, nsAString& aResult) const;
  void SetStringBaseValue(uint8_t aAttrEnum, const nsAString& aValue);

  virtual nsStaticAtom* GetPointListAttrName() const { return nullptr; }
  virtual nsStaticAtom* GetPathDataAttrName() const { return nullptr; }
  virtual nsStaticAtom* GetTransformListAttrName() const { return nullptr; }
  const nsAttrValue* GetAnimatedClassName() const {
    if (!mClassAttribute.IsAnimated()) {
      return nullptr;
    }
    return mClassAnimAttr.get();
  }

  virtual void ClearAnyCachedPath() {}
  virtual bool IsTransformable() { return false; }

  // WebIDL
  mozilla::dom::SVGSVGElement* GetOwnerSVGElement();
  SVGElement* GetViewportElement();
  already_AddRefed<mozilla::dom::DOMSVGAnimatedString> ClassName();

  bool Autofocus() const { return GetBoolAttr(nsGkAtoms::autofocus); }
  void SetAutofocus(bool aAutofocus, ErrorResult& aRv) {
    if (aAutofocus) {
      SetAttr(nsGkAtoms::autofocus, u""_ns, aRv);
    } else {
      UnsetAttr(nsGkAtoms::autofocus, aRv);
    }
  }

 protected:
  JSObject* WrapNode(JSContext* cx, JS::Handle<JSObject*> aGivenProto) override;

  // We define BeforeSetAttr here and mark it final to ensure it is NOT used
  // by SVG elements.
  // This is because we're not currently passing the correct value for aValue to
  // BeforeSetAttr since it would involve allocating extra SVG value types.
  // See the comment in SVGElement::WillChangeValue.
  void BeforeSetAttr(int32_t aNamespaceID, nsAtom* aName,
                     const nsAttrValue* aValue, bool aNotify) final;
  void AfterSetAttr(int32_t aNamespaceID, nsAtom* aName,
                    const nsAttrValue* aValue, const nsAttrValue* aOldValue,
                    nsIPrincipal* aSubjectPrincipal, bool aNotify) override;
  bool ParseAttribute(int32_t aNamespaceID, nsAtom* aAttribute,
                      const nsAString& aValue,
                      nsIPrincipal* aMaybeScriptedPrincipal,
                      nsAttrValue& aResult) override;
  static nsresult ReportAttributeParseFailure(Document* aDocument,
                                              nsAtom* aAttribute,
                                              const nsAString& aValue);

  nsAttrValue WillChangeValue(nsAtom* aName,
                              const mozAutoDocUpdate& aProofOfUpdate);
  // aNewValue is set to the old value. This value may be invalid if
  // !StoresOwnData.
  void DidChangeValue(nsAtom* aName, const nsAttrValue& aEmptyOrOldValue,
                      nsAttrValue& aNewValue,
                      const mozAutoDocUpdate& aProofOfUpdate);
  void MaybeSerializeAttrBeforeRemoval(nsAtom* aName, bool aNotify);

  nsAtom* GetEventNameForAttr(nsAtom* aAttr) override;

  struct LengthInfo {
    nsStaticAtom* const mName;
    const float mDefaultValue;
    const uint8_t mDefaultUnitType;
    const uint8_t mCtxType;
  };

  template <typename Value, typename InfoValue>
  struct AttributesInfo {
    Value* const mValues;
    const InfoValue* const mInfos;
    const uint32_t mCount;

    AttributesInfo(Value* aValues, const InfoValue* aInfos, uint32_t aCount)
        : mValues(aValues), mInfos(aInfos), mCount(aCount) {}

    void CopyAllFrom(const AttributesInfo&);
    void ResetAll();
    void Reset(uint8_t aEnum);
  };

  using LengthAttributesInfo = AttributesInfo<SVGAnimatedLength, LengthInfo>;

  struct NumberInfo {
    nsStaticAtom* const mName;
    const float mDefaultValue;
  };

  using NumberAttributesInfo = AttributesInfo<SVGAnimatedNumber, NumberInfo>;

  struct NumberPairInfo {
    nsStaticAtom* const mName;
    const float mDefaultValue1;
    const float mDefaultValue2;
  };

  using NumberPairAttributesInfo =
      AttributesInfo<SVGAnimatedNumberPair, NumberPairInfo>;

  struct IntegerInfo {
    nsStaticAtom* const mName;
    const int32_t mDefaultValue;
  };

  using IntegerAttributesInfo = AttributesInfo<SVGAnimatedInteger, IntegerInfo>;

  struct IntegerPairInfo {
    nsStaticAtom* const mName;
    const int32_t mDefaultValue1;
    const int32_t mDefaultValue2;
  };

  using IntegerPairAttributesInfo =
      AttributesInfo<SVGAnimatedIntegerPair, IntegerPairInfo>;

  struct BooleanInfo {
    nsStaticAtom* const mName;
    const bool mDefaultValue;
  };

  using BooleanAttributesInfo = AttributesInfo<SVGAnimatedBoolean, BooleanInfo>;

  friend class mozilla::SVGAnimatedEnumeration;

  struct EnumInfo {
    nsStaticAtom* const mName;
    const SVGEnumMapping* const mMapping;
    const uint16_t mDefaultValue;
  };

  using EnumAttributesInfo = AttributesInfo<SVGAnimatedEnumeration, EnumInfo>;

  struct NumberListInfo {
    nsStaticAtom* const mName;
  };

  using NumberListAttributesInfo =
      AttributesInfo<SVGAnimatedNumberList, NumberListInfo>;

  struct LengthListInfo {
    nsStaticAtom* const mName;
    const uint8_t mAxis;
    /**
     * Flag to indicate whether appending zeros to the end of the list would
     * change the rendering of the SVG for the attribute in question. For x and
     * y on the <text> element this is true, but for dx and dy on <text> this
     * is false. This flag is fed down to SVGLengthListSMILType so it can
     * determine if it can sensibly animate from-to lists of different lengths,
     * which is desirable in the case of dx and dy.
     */
    const bool mCouldZeroPadList;
  };

  using LengthListAttributesInfo =
      AttributesInfo<SVGAnimatedLengthList, LengthListInfo>;

  struct StringInfo {
    nsStaticAtom* const mName;
    const int32_t mNamespaceID;
    const bool mIsAnimatable;
  };

  using StringAttributesInfo = AttributesInfo<SVGAnimatedString, StringInfo>;

  friend class DOMSVGStringList;

  struct StringListInfo {
    nsStaticAtom* const mName;
  };

  using StringListAttributesInfo =
      AttributesInfo<SVGStringList, StringListInfo>;

  virtual LengthAttributesInfo GetLengthInfo();
  virtual NumberAttributesInfo GetNumberInfo();
  virtual NumberPairAttributesInfo GetNumberPairInfo();
  virtual IntegerAttributesInfo GetIntegerInfo();
  virtual IntegerPairAttributesInfo GetIntegerPairInfo();
  virtual BooleanAttributesInfo GetBooleanInfo();
  virtual EnumAttributesInfo GetEnumInfo();
  // We assume all orients, viewboxes and preserveAspectRatios are alike
  // so we don't need to wrap the class
  virtual SVGAnimatedOrient* GetAnimatedOrient();
  virtual SVGAnimatedPreserveAspectRatio* GetAnimatedPreserveAspectRatio();
  virtual SVGAnimatedViewBox* GetAnimatedViewBox();
  virtual NumberListAttributesInfo GetNumberListInfo();
  virtual LengthListAttributesInfo GetLengthListInfo();
  virtual StringAttributesInfo GetStringInfo();
  virtual StringListAttributesInfo GetStringListInfo();

  static SVGEnumMapping sSVGUnitTypesMap[];

  virtual void DidAnimateAttribute(int32_t aNameSpaceID, nsAtom* aAttribute);

 private:
  void UnsetAttrInternal(int32_t aNameSpaceID, nsAtom* aName, bool aNotify);

  SVGAnimatedClass mClassAttribute;
  UniquePtr<nsAttrValue> mClassAnimAttr;
};

/**
 * A macro to implement the NS_NewSVGXXXElement() functions.
 */
#define NS_IMPL_NS_NEW_SVG_ELEMENT(_elementName)                               \
  nsresult NS_NewSVG##_elementName##Element(                                   \
      nsIContent** aResult,                                                    \
      already_AddRefed<mozilla::dom::NodeInfo>&& aNodeInfo) {                  \
    RefPtr<mozilla::dom::NodeInfo> nodeInfo(aNodeInfo);                        \
    auto* nim = nodeInfo->NodeInfoManager();                                   \
    RefPtr<mozilla::dom::SVG##_elementName##Element> it =                      \
        new (nim) mozilla::dom::SVG##_elementName##Element(nodeInfo.forget()); \
                                                                               \
    nsresult rv = it->Init();                                                  \
                                                                               \
    if (NS_FAILED(rv)) {                                                       \
      return rv;                                                               \
    }                                                                          \
                                                                               \
    it.forget(aResult);                                                        \
                                                                               \
    return rv;                                                                 \
  }

#define NS_IMPL_NS_NEW_SVG_ELEMENT_CHECK_PARSER(_elementName)                 \
  nsresult NS_NewSVG##_elementName##Element(                                  \
      nsIContent** aResult,                                                   \
      already_AddRefed<mozilla::dom::NodeInfo>&& aNodeInfo,                   \
      mozilla::dom::FromParser aFromParser) {                                 \
    RefPtr<mozilla::dom::NodeInfo> nodeInfo(aNodeInfo);                       \
    auto* nim = nodeInfo->NodeInfoManager();                                  \
    RefPtr<mozilla::dom::SVG##_elementName##Element> it =                     \
        new (nim) mozilla::dom::SVG##_elementName##Element(nodeInfo.forget(), \
                                                           aFromParser);      \
                                                                              \
    nsresult rv = it->Init();                                                 \
                                                                              \
    if (NS_FAILED(rv)) {                                                      \
      return rv;                                                              \
    }                                                                         \
                                                                              \
    it.forget(aResult);                                                       \
                                                                              \
    return rv;                                                                \
  }

// No unlinking, we'd need to null out the value pointer (the object it
// points to is held by the element) and null-check it everywhere.
#define NS_SVG_VAL_IMPL_CYCLE_COLLECTION(_val, _element) \
  NS_IMPL_CYCLE_COLLECTION_CLASS(_val)                   \
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(_val)          \
    NS_IMPL_CYCLE_COLLECTION_TRAVERSE(_element)          \
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END                  \
  NS_IMPL_CYCLE_COLLECTION_UNLINK_0(_val)

#define NS_SVG_VAL_IMPL_CYCLE_COLLECTION_WRAPPERCACHED(_val, _element) \
  NS_IMPL_CYCLE_COLLECTION_CLASS(_val)                                 \
  NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(_val)                          \
    NS_IMPL_CYCLE_COLLECTION_UNLINK_PRESERVED_WRAPPER                  \
  NS_IMPL_CYCLE_COLLECTION_UNLINK_END                                  \
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(_val)                        \
    NS_IMPL_CYCLE_COLLECTION_TRAVERSE(_element)                        \
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END                                \
  NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN(_val)                           \
    NS_IMPL_CYCLE_COLLECTION_TRACE_PRESERVED_WRAPPER                   \
  NS_IMPL_CYCLE_COLLECTION_TRACE_END

}  // namespace dom
}  // namespace mozilla

#endif  // DOM_SVG_SVGELEMENT_H_
