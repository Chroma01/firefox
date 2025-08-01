/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * compute sticky positioning, both during reflow and when the scrolling
 * container scrolls
 */

#include "StickyScrollContainer.h"

#include "PresShell.h"
#include "mozilla/OverflowChangedTracker.h"
#include "mozilla/ScrollContainerFrame.h"
#include "nsIFrame.h"
#include "nsIFrameInlines.h"
#include "nsLayoutUtils.h"

namespace mozilla {

NS_DECLARE_FRAME_PROPERTY_DELETABLE(StickyScrollContainerProperty,
                                    StickyScrollContainer)

StickyScrollContainer::StickyScrollContainer(
    ScrollContainerFrame* aScrollContainerFrame)
    : mScrollContainerFrame(aScrollContainerFrame) {
  mScrollContainerFrame->AddScrollPositionListener(this);
}

StickyScrollContainer::~StickyScrollContainer() {
  mScrollContainerFrame->RemoveScrollPositionListener(this);
}

// static
StickyScrollContainer* StickyScrollContainer::GetStickyScrollContainerForFrame(
    nsIFrame* aFrame) {
  ScrollContainerFrame* scrollContainerFrame =
      nsLayoutUtils::GetNearestScrollContainerFrame(
          aFrame->GetParent(), nsLayoutUtils::SCROLLABLE_SAME_DOC |
                                   nsLayoutUtils::SCROLLABLE_STOP_AT_PAGE |
                                   nsLayoutUtils::SCROLLABLE_INCLUDE_HIDDEN);
  if (!scrollContainerFrame) {
    // We might not find any, for instance in the case of
    // <html style="position: fixed">
    return nullptr;
  }
  StickyScrollContainer* s =
      scrollContainerFrame->GetProperty(StickyScrollContainerProperty());
  if (!s) {
    s = new StickyScrollContainer(scrollContainerFrame);
    scrollContainerFrame->SetProperty(StickyScrollContainerProperty(), s);
  }
  return s;
}

// static
StickyScrollContainer*
StickyScrollContainer::GetStickyScrollContainerForScrollFrame(
    nsIFrame* aFrame) {
  return aFrame->GetProperty(StickyScrollContainerProperty());
}

static nscoord ComputeStickySideOffset(Side aSide,
                                       const nsStylePosition& aPosition,
                                       nscoord aPercentBasis) {
  // Guaranteed to resolve any use of anchor function as invalid.
  const auto& side = aPosition.GetAnchorResolvedInset(
      aSide, AnchorPosOffsetResolutionParams::UseCBFrameSize(
                 {nullptr, StylePositionProperty::Sticky}));
  if (side->IsAuto()) {
    return NS_AUTOOFFSET;
  }
  return nsLayoutUtils::ComputeCBDependentValue(aPercentBasis,
                                                side->AsLengthPercentage());
}

// static
void StickyScrollContainer::ComputeStickyOffsets(nsIFrame* aFrame) {
  ScrollContainerFrame* scrollContainerFrame =
      nsLayoutUtils::GetNearestScrollContainerFrame(
          aFrame->GetParent(), nsLayoutUtils::SCROLLABLE_SAME_DOC |
                                   nsLayoutUtils::SCROLLABLE_INCLUDE_HIDDEN);

  if (!scrollContainerFrame) {
    // Bail.
    return;
  }

  nsSize scrollContainerSize =
      scrollContainerFrame->GetScrolledFrameSizeAccountingForDynamicToolbar();

  nsMargin computedOffsets;
  const nsStylePosition* position = aFrame->StylePosition();

  computedOffsets.left =
      ComputeStickySideOffset(eSideLeft, *position, scrollContainerSize.width);
  computedOffsets.right =
      ComputeStickySideOffset(eSideRight, *position, scrollContainerSize.width);
  computedOffsets.top =
      ComputeStickySideOffset(eSideTop, *position, scrollContainerSize.height);
  computedOffsets.bottom = ComputeStickySideOffset(eSideBottom, *position,
                                                   scrollContainerSize.height);

  // Store the offset
  nsMargin* offsets = aFrame->GetProperty(nsIFrame::ComputedOffsetProperty());
  if (offsets) {
    *offsets = computedOffsets;
  } else {
    aFrame->SetProperty(nsIFrame::ComputedOffsetProperty(),
                        new nsMargin(computedOffsets));
  }
}

static constexpr nscoord gUnboundedNegative = nscoord_MIN / 2;
static constexpr nscoord gUnboundedExtent = nscoord_MAX;
static constexpr nscoord gUnboundedPositive =
    gUnboundedNegative + gUnboundedExtent;

void StickyScrollContainer::ComputeStickyLimits(nsIFrame* aFrame,
                                                nsRect* aStick,
                                                nsRect* aContain) const {
  NS_ASSERTION(nsLayoutUtils::IsFirstContinuationOrIBSplitSibling(aFrame),
               "Can't sticky position individual continuations");

  aStick->SetRect(gUnboundedNegative, gUnboundedNegative, gUnboundedExtent,
                  gUnboundedExtent);
  aContain->SetRect(gUnboundedNegative, gUnboundedNegative, gUnboundedExtent,
                    gUnboundedExtent);

  const nsMargin* computedOffsets =
      aFrame->GetProperty(nsIFrame::ComputedOffsetProperty());
  if (!computedOffsets) {
    // We haven't reflowed the scroll frame yet, so offsets haven't been
    // computed. Bail.
    return;
  }

  nsIFrame* scrolledFrame = mScrollContainerFrame->GetScrolledFrame();
  nsIFrame* cbFrame = aFrame->GetContainingBlock();
  NS_ASSERTION(cbFrame == scrolledFrame ||
                   nsLayoutUtils::IsProperAncestorFrame(scrolledFrame, cbFrame),
               "Scroll frame should be an ancestor of the containing block");

  nsRect rect =
      nsLayoutUtils::GetAllInFlowRectsUnion(aFrame, aFrame->GetParent());

  // FIXME(bug 1421660): Table row groups aren't supposed to be containing
  // blocks, but we treat them as such (maybe it's the right thing to do!).
  // Anyway, not having this basically disables position: sticky on table cells,
  // which would be really unfortunate, and doesn't match what other browsers
  // do.
  if (cbFrame != scrolledFrame && cbFrame->IsTableRowGroupFrame()) {
    cbFrame = cbFrame->GetContainingBlock();
  }

  // Containing block limits for the position of aFrame relative to its parent.
  // The margin box of the sticky element stays within the content box of the
  // containing-block element.
  if (cbFrame == scrolledFrame) {
    // cbFrame is the scrolledFrame, and it won't have continuations. Unlike the
    // else clause, we consider scrollable overflow rect because the union of
    // its in-flow rects doesn't include the scrollable overflow area. We need
    // to subtract the padding however, which _is_ included in the scrollable
    // area, since we want the content box.
    MOZ_ASSERT(cbFrame->GetUsedBorder() == nsMargin(),
               "How did the ::-moz-scrolled-frame end up with border?");
    *aContain = cbFrame->ScrollableOverflowRectRelativeToSelf();
    aContain->Deflate(cbFrame->GetUsedPadding());
    nsLayoutUtils::TransformRect(cbFrame, aFrame->GetParent(), *aContain);
  } else {
    *aContain = nsLayoutUtils::GetAllInFlowRectsUnion(
        cbFrame, aFrame->GetParent(),
        nsLayoutUtils::GetAllInFlowRectsFlag::UseContentBox);
  }

  nsRect marginRect = nsLayoutUtils::GetAllInFlowRectsUnion(
      aFrame, aFrame->GetParent(),
      nsLayoutUtils::GetAllInFlowRectsFlag::UseMarginBoxWithAutoResolvedAsZero);

  // Deflate aContain by the difference between the union of aFrame's
  // continuations' margin boxes and the union of their border boxes, so that
  // by keeping aFrame within aContain, we keep the union of the margin boxes
  // within the containing block's content box.
  aContain->Deflate(marginRect - rect);

  // Deflate aContain by the border-box size, to form a constraint on the
  // upper-left corner of aFrame and continuations.
  aContain->Deflate(nsMargin(0, rect.width, rect.height, 0));

  nsMargin sfPadding = scrolledFrame->GetUsedPadding();
  nsPoint sfOffset = aFrame->GetParent()->GetOffsetTo(scrolledFrame);

  // Top
  if (computedOffsets->top != NS_AUTOOFFSET) {
    aStick->SetTopEdge(mScrollPosition.y + sfPadding.top +
                       computedOffsets->top - sfOffset.y);
  }

  nsSize sfSize =
      mScrollContainerFrame->GetScrolledFrameSizeAccountingForDynamicToolbar();

  // Bottom
  if (computedOffsets->bottom != NS_AUTOOFFSET &&
      (computedOffsets->top == NS_AUTOOFFSET ||
       rect.height <= sfSize.height - computedOffsets->TopBottom())) {
    aStick->SetBottomEdge(mScrollPosition.y + sfPadding.top + sfSize.height -
                          computedOffsets->bottom - rect.height - sfOffset.y);
  }

  StyleDirection direction = cbFrame->StyleVisibility()->mDirection;

  // Left
  if (computedOffsets->left != NS_AUTOOFFSET &&
      (computedOffsets->right == NS_AUTOOFFSET ||
       direction == StyleDirection::Ltr ||
       rect.width <= sfSize.width - computedOffsets->LeftRight())) {
    aStick->SetLeftEdge(mScrollPosition.x + sfPadding.left +
                        computedOffsets->left - sfOffset.x);
  }

  // Right
  if (computedOffsets->right != NS_AUTOOFFSET &&
      (computedOffsets->left == NS_AUTOOFFSET ||
       direction == StyleDirection::Rtl ||
       rect.width <= sfSize.width - computedOffsets->LeftRight())) {
    aStick->SetRightEdge(mScrollPosition.x + sfPadding.left + sfSize.width -
                         computedOffsets->right - rect.width - sfOffset.x);
  }

  // These limits are for the bounding box of aFrame's continuations. Convert
  // to limits for aFrame itself.
  nsPoint frameOffset = aFrame->GetPosition() - rect.TopLeft();
  aStick->MoveBy(frameOffset);
  aContain->MoveBy(frameOffset);
}

nsPoint StickyScrollContainer::ComputePosition(nsIFrame* aFrame) const {
  nsRect stick;
  nsRect contain;
  ComputeStickyLimits(aFrame, &stick, &contain);

  nsPoint position = aFrame->GetNormalPosition();

  // For each sticky direction (top, bottom, left, right), move the frame along
  // the appropriate axis, based on the scroll position, but limit this to keep
  // the element's margin box within the containing block.
  position.y = std::max(position.y, std::min(stick.y, contain.YMost()));
  position.y = std::min(position.y, std::max(stick.YMost(), contain.y));
  position.x = std::max(position.x, std::min(stick.x, contain.XMost()));
  position.x = std::min(position.x, std::max(stick.XMost(), contain.x));

  return position;
}

bool StickyScrollContainer::IsStuckInYDirection(nsIFrame* aFrame) const {
  nsPoint position = ComputePosition(aFrame);
  return position.y != aFrame->GetNormalPosition().y;
}

void StickyScrollContainer::GetScrollRanges(nsIFrame* aFrame,
                                            nsRectAbsolute* aOuter,
                                            nsRectAbsolute* aInner) const {
  // We need to use the first in flow; continuation frames should not move
  // relative to each other and should get identical scroll ranges.
  // Also, ComputeStickyLimits requires this.
  nsIFrame* firstCont =
      nsLayoutUtils::FirstContinuationOrIBSplitSibling(aFrame);

  nsRect stickRect;
  nsRect containRect;
  ComputeStickyLimits(firstCont, &stickRect, &containRect);

  nsRectAbsolute stick = nsRectAbsolute::FromRect(stickRect);
  nsRectAbsolute contain = nsRectAbsolute::FromRect(containRect);

  aOuter->SetBox(gUnboundedNegative, gUnboundedNegative, gUnboundedPositive,
                 gUnboundedPositive);
  aInner->SetBox(gUnboundedNegative, gUnboundedNegative, gUnboundedPositive,
                 gUnboundedPositive);

  const nsPoint normalPosition = firstCont->GetNormalPosition();

  // Bottom and top
  if (stick.YMost() != gUnboundedPositive) {
    aOuter->SetTopEdge(contain.Y() - stick.YMost());
    aInner->SetTopEdge(normalPosition.y - stick.YMost());
  }

  if (stick.Y() != gUnboundedNegative) {
    aInner->SetBottomEdge(normalPosition.y - stick.Y());
    aOuter->SetBottomEdge(contain.YMost() - stick.Y());
  }

  // Right and left
  if (stick.XMost() != gUnboundedPositive) {
    aOuter->SetLeftEdge(contain.X() - stick.XMost());
    aInner->SetLeftEdge(normalPosition.x - stick.XMost());
  }

  if (stick.X() != gUnboundedNegative) {
    aInner->SetRightEdge(normalPosition.x - stick.X());
    aOuter->SetRightEdge(contain.XMost() - stick.X());
  }

  // Make sure |inner| does not extend outside of |outer|. (The consumers of
  // the Layers API, to which this information is propagated, expect this
  // invariant to hold.) The calculated value of |inner| can sometimes extend
  // outside of |outer|, for example due to margin collapsing, since
  // GetNormalPosition() returns the actual position after margin collapsing,
  // while |contain| is calculated based on the frame's GetUsedMargin() which
  // is pre-collapsing.
  // Note that this doesn't necessarily solve all problems stemming from
  // comparing pre- and post-collapsing margins (TODO: find a proper solution).
  *aInner = aInner->Intersect(*aOuter);
  if (aInner->IsEmpty()) {
    // This might happen if aInner didn't intersect aOuter at all initially,
    // in which case aInner is empty and outside aOuter. Make sure it doesn't
    // extend outside aOuter.
    *aInner = aInner->MoveInsideAndClamp(*aOuter);
  }
}

void StickyScrollContainer::PositionContinuations(nsIFrame* aFrame) {
  NS_ASSERTION(nsLayoutUtils::IsFirstContinuationOrIBSplitSibling(aFrame),
               "Should be starting from the first continuation");
  bool hadProperty;
  nsPoint translation =
      ComputePosition(aFrame) - aFrame->GetNormalPosition(&hadProperty);
  if (NS_WARN_IF(!hadProperty)) {
    // If the frame was never relatively positioned, don't move its position
    // dynamically. There are a variety of frames for which `position` doesn't
    // really apply like frames inside svg which would get here and be sticky
    // only in one direction.
    return;
  }

  // Move all continuation frames by the same amount.
  for (nsIFrame* cont = aFrame; cont;
       cont = nsLayoutUtils::GetNextContinuationOrIBSplitSibling(cont)) {
    cont->SetPosition(cont->GetNormalPosition() + translation);
  }
}

void StickyScrollContainer::UpdatePositions(nsPoint aScrollPosition,
                                            nsIFrame* aSubtreeRoot) {
#ifdef DEBUG
  {
    nsIFrame* scrollFrameAsFrame = do_QueryFrame(mScrollContainerFrame);
    NS_ASSERTION(!aSubtreeRoot || aSubtreeRoot == scrollFrameAsFrame,
                 "If reflowing, should be reflowing the scroll frame");
  }
#endif
  mScrollPosition = aScrollPosition;

  OverflowChangedTracker oct;
  oct.SetSubtreeRoot(aSubtreeRoot);
  // We need to position ancestors before children, so iter from shallowest. We
  // don't use mFrames.IterFromShallowest() because we want to handle removal of
  // non-first continuations while we iterate.
  for (size_t i = mFrames.Length(); i--;) {
    nsIFrame* f = mFrames.ElementAt(i);
    if (!nsLayoutUtils::IsFirstContinuationOrIBSplitSibling(f)) {
      // This frame was added in nsIFrame::DidSetComputedStyle before we knew it
      // wasn't the first ib-split-sibling.
      mFrames.RemoveElementAt(i);
      continue;
    }
    if (aSubtreeRoot) {
      // Reflowing the scroll frame, so recompute offsets.
      ComputeStickyOffsets(f);
    }
    // mFrames will only contain first continuations, because we filter in
    // nsIFrame::DidSetComputedStyle.
    PositionContinuations(f);

    f = f->GetParent();
    if (f != aSubtreeRoot) {
      for (nsIFrame* cont = f; cont;
           cont = nsLayoutUtils::GetNextContinuationOrIBSplitSibling(cont)) {
        oct.AddFrame(cont, OverflowChangedTracker::CHILDREN_CHANGED);
      }
    }
  }
  oct.Flush();
}

void StickyScrollContainer::ScrollPositionWillChange(nscoord aX, nscoord aY) {}

void StickyScrollContainer::ScrollPositionDidChange(nscoord aX, nscoord aY) {
  UpdatePositions(nsPoint(aX, aY), nullptr);
}

void StickyScrollContainer::MarkFramesForReflow() {
  PresShell* ps = mScrollContainerFrame->PresShell();
  for (nsIFrame* frame : mFrames.IterFromShallowest()) {
    ps->FrameNeedsReflow(frame, IntrinsicDirty::None, NS_FRAME_IS_DIRTY);
  }
}
}  // namespace mozilla
