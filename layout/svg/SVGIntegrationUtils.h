/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef LAYOUT_SVG_SVGINTEGRATIONUTILS_H_
#define LAYOUT_SVG_SVGINTEGRATIONUTILS_H_

#include "ImgDrawResult.h"
#include "gfxMatrix.h"
#include "gfxRect.h"
#include "mozilla/ServoStyleConsts.h"
#include "mozilla/gfx/Rect.h"
#include "mozilla/webrender/WebRenderTypes.h"
#include "nsRegionFwd.h"

class gfxContext;
class gfxDrawable;
class nsIFrame;
struct nsPoint;
struct nsRect;
struct nsSize;

enum class WrFiltersStatus {
  // Image will be rendered unftilered - the filter graph contains invalid refs
  // (which SVG spec states will be rendered as if there is no filter graph).
  UNSUPPORTED = 0,
  // Image will be rendered unfiltered - the filter graph provided is
  // excessively costly to render and has been dropped (per SVG spec we can do
  // this to preserve user experience).
  DISABLED_FOR_PERFORMANCE = 1,
  // Image will be rendered using blob fallback (software rendering) due to
  // unsupported operations (in either the CSS or SVGFE path).
  BLOB_FALLBACK = 2,
  // Image will be rendered using a simple CSS filter chain in WebRender.
  CHAIN = 3,
  // Filter graph will be rendered using WebRender SVGFE code path, this can
  // handle any kind of filter graph consisting of supported operations (most
  // operations are supported).
  SVGFE = 4,
};

struct WrFiltersHolder {
  // TODO(Bug 1899691): Better to use AutoTArray here...
  nsTArray<mozilla::wr::FilterOp> filters;
  nsTArray<mozilla::wr::WrFilterData> filter_datas;
  mozilla::Maybe<nsRect> post_filters_clip;
  // This exists just to own the values long enough for them to be copied into
  // rust.
  nsTArray<nsTArray<float>> values;
};

namespace mozilla {
class nsDisplayList;
class nsDisplayListBuilder;

/**
 * Whether we're dealing with a backdrop-filter or a filter.
 */
enum class StyleFilterType : uint8_t { BackdropFilter, Filter };

namespace gfx {
class DrawTarget;
}  // namespace gfx

/**
 * Integration of SVG effects (clipPath clipping, masking and filters) into
 * regular display list based painting and hit-testing.
 */
class SVGIntegrationUtils final {
  using DrawTarget = gfx::DrawTarget;
  using IntRect = gfx::IntRect;
  using imgDrawingParams = image::imgDrawingParams;

 public:
  /**
   * Returns true if SVG effects that affect the overflow of the given frame
   * are currently applied to the frame.
   */
  static bool UsingOverflowAffectingEffects(const nsIFrame* aFrame);

  /**
   * Returns true if SVG effects are currently applied to this frame.
   */
  static bool UsingEffectsForFrame(const nsIFrame* aFrame);

  /**
   * Returns the size of the union of the border-box rects of all of
   * aNonSVGFrame's continuations.
   */
  static nsSize GetContinuationUnionSize(nsIFrame* aNonSVGFrame);

  /**
   * When SVG effects need to resolve percentage, userSpaceOnUse lengths, they
   * need a coordinate context to resolve them against. This method provides
   * that coordinate context for non-SVG frames with SVG effects applied to
   * them. The gfxSize returned is the size of the union of all of the given
   * frame's continuations' border boxes, converted to SVG user units (equal to
   * CSS px units), as required by the SVG code.
   */
  static gfx::Size GetSVGCoordContextForNonSVGFrame(nsIFrame* aNonSVGFrame);

  /**
   * SVG effects such as SVG filters, masking and clipPath may require an SVG
   * "bbox" for the element they're being applied to in order to make decisions
   * about positioning, and to resolve various lengths against. This method
   * provides the "bbox" for non-SVG frames. The bbox returned is in CSS px
   * units, and aUnionContinuations decide whether bbox contains the area of
   * current frame only or the union of all aNonSVGFrame's continuations'
   * overflow areas, relative to the top-left of the union of all aNonSVGFrame's
   * continuations' border box rects.
   */
  static gfxRect GetSVGBBoxForNonSVGFrame(nsIFrame* aNonSVGFrame,
                                          bool aUnionContinuations);

  /**
   * Used to adjust a frame's pre-effects ink overflow rect to take account
   * of SVG effects.
   *
   * XXX This method will not do the right thing for frames with continuations.
   * It really needs all the continuations to have been reflowed before being
   * called, but we currently call it on each continuation as its overflow
   * rects are set during the reflow of each particular continuation. Gecko's
   * current reflow architecture does not allow us to set the overflow rects
   * for a whole chain of continuations for a given element at the point when
   * the last continuation is reflowed. See:
   * http://groups.google.com/group/mozilla.dev.tech.layout/msg/6b179066f3051f65
   */
  static nsRect ComputePostEffectsInkOverflowRect(
      nsIFrame* aFrame, const nsRect& aPreEffectsOverflowRect);

  /**
   * Figure out which area of the source is needed given an area to
   * repaint
   */
  static nsRect GetRequiredSourceForInvalidArea(nsIFrame* aFrame,
                                                const nsRect& aDirtyRect);

  /**
   * Returns true if the given point is not clipped out by effects.
   * @param aPt in appunits relative to aFrame
   */
  static bool HitTestFrameForEffects(nsIFrame* aFrame, const nsPoint& aPt);

  struct MOZ_STACK_CLASS PaintFramesParams {
    gfxContext& ctx;
    nsIFrame* frame;
    nsRect dirtyRect;
    nsRect borderArea;
    nsDisplayListBuilder* builder;
    bool handleOpacity;  // If true, PaintMaskAndClipPath/ PaintFilter should
                         // apply css opacity.
    Maybe<LayoutDeviceRect> maskRect;
    imgDrawingParams& imgParams;

    explicit PaintFramesParams(gfxContext& aCtx, nsIFrame* aFrame,
                               const nsRect& aDirtyRect,
                               const nsRect& aBorderArea,
                               nsDisplayListBuilder* aBuilder,
                               bool aHandleOpacity,
                               imgDrawingParams& aImgParams)
        : ctx(aCtx),
          frame(aFrame),
          dirtyRect(aDirtyRect),
          borderArea(aBorderArea),
          builder(aBuilder),
          handleOpacity(aHandleOpacity),
          imgParams(aImgParams) {}
  };

  // This should use FunctionRef instead of std::function because we don't need
  // to take ownership of the function. See bug 1490781.
  static void PaintMaskAndClipPath(const PaintFramesParams& aParams,
                                   const std::function<void()>& aPaintChild);

  /**
   * Paint mask of frame onto a given context, aParams.ctx.
   * aParams.ctx must contain an A8 surface. Returns false if the mask
   * didn't get painted and should be ignored at the call site.
   * isMaskComplete is an outparameter returning whether the mask is complete.
   * Incomplete masks should not be drawn and the proper fallback behaviour
   * depends on if the masked element is html or svg.
   */
  static bool PaintMask(const PaintFramesParams& aParams,
                        bool& aOutIsMaskComplete);

  /**
   * Paint the frame contents.
   * SVG frames will have had matrix propagation set to false already.
   * Non-SVG frames have to do their own thing.
   * The caller will do a Save()/Restore() as necessary so feel free
   * to mess with context state.
   * The context will be configured to use the "user space" coordinate
   * system if passing aTransform/aDirtyRect, or untouched otherwise.
   * @param aImgParams the params to draw with.
   * @param aTransform the user-to-device space matrix, if painting with
   *        filters.
   * @param aDirtyRect the dirty rect *in user space pixels*
   */
  using SVGFilterPaintCallback = std::function<void(
      gfxContext& aContext, imgDrawingParams&, const gfxMatrix* aTransform,
      const nsIntRect* aDirtyRect)>;

  /**
   * Paint non-SVG frame with filter and opacity effect.
   */
  static void PaintFilter(const PaintFramesParams& aParams,
                          Span<const StyleFilter> aFilters,
                          const SVGFilterPaintCallback& aCallback);

  /**
   * Build WebRender filters for a frame with CSS filters applied to it.
   */
  static WrFiltersStatus CreateWebRenderCSSFilters(
      Span<const StyleFilter> aFilters, nsIFrame* aFrame,
      WrFiltersHolder& aWrFilters);

  /**
   * Try to build WebRender filters for a frame with SVG filters applied to it
   * if the filters are supported.
   */
  static WrFiltersStatus BuildWebRenderFilters(
      nsIFrame* aFilteredFrame, Span<const StyleFilter> aFilters,
      StyleFilterType aStyleFilterType, WrFiltersHolder& aWrFilters,
      const nsPoint& aOffsetForSVGFilters);

  /**
   * Check if the filters present on |aFrame| are supported by WebRender.
   */
  static bool CanCreateWebRenderFiltersForFrame(nsIFrame* aFrame);

  /**
   * Check if |aFrame| uses any SVG effects that cannot be rendered in the
   * compositor.
   */
  static bool UsesSVGEffectsNotSupportedInCompositor(nsIFrame* aFrame);

  /**
   * @param aRenderingContext the target rendering context in which the paint
   * server will be rendered
   * @param aTarget the target frame onto which the paint server will be
   * rendered
   * @param aPaintServer a first-continuation frame to use as the source
   * @param aFilter a filter to be applied when scaling
   * @param aDest the area the paint server image should be mapped to
   * @param aFill the area to be filled with copies of the paint server image
   * @param aAnchor a point in aFill which we will ensure is pixel-aligned in
   * the output
   * @param aDirty pixels outside this area may be skipped
   * @param aPaintServerSize the size that would be filled when using
   * background-repeat:no-repeat and background-size:auto. For normal background
   * images, this would be the intrinsic size of the image; for gradients and
   * patterns this would be the whole target frame fill area.
   * @param aFlags pass FLAG_SYNC_DECODE_IMAGES and any images in the paint
   * server will be decoding synchronously if they are not decoded already.
   */
  enum {
    FLAG_SYNC_DECODE_IMAGES = 0x01,
  };

  static already_AddRefed<gfxDrawable> DrawableFromPaintServer(
      nsIFrame* aFrame, nsIFrame* aTarget, const nsSize& aPaintServerSize,
      const gfx::IntSize& aRenderSize, const DrawTarget* aDrawTarget,
      const gfxMatrix& aContextMatrix, uint32_t aFlags);

  /**
   * For non-SVG frames, this gives the offset to the frame's "user space".
   * For SVG frames, this returns a zero offset.
   */
  static nsPoint GetOffsetToBoundingBox(nsIFrame* aFrame);

  /**
   * The offset between the reference frame and the bounding box of the
   * target frame in device units.
   */
  static gfxPoint GetOffsetToUserSpaceInDevPx(nsIFrame* aFrame,
                                              const PaintFramesParams& aParams);
};

}  // namespace mozilla

#endif  // LAYOUT_SVG_SVGINTEGRATIONUTILS_H_
