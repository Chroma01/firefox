/* -*- Mode: IDL; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * The origin of this IDL file is
 * https://w3c.github.io/selection-api/#selection-interface
 *
 * Copyright © 2012 W3C® (MIT, ERCIM, Keio), All Rights Reserved. W3C
 * liability, trademark and document use rules apply.
 */

// https://www.w3.org/TR/selection-api/#dom-getcomposedrangesoptions
dictionary GetComposedRangesOptions {
  sequence<ShadowRoot> shadowRoots = [];
};

[Exposed=Window]
interface Selection {
  [NeedsCallerType]
  readonly attribute Node?         anchorNode;
  [NeedsCallerType]
  readonly attribute unsigned long anchorOffset;
  [NeedsCallerType]
  readonly attribute Node?         focusNode;
  [NeedsCallerType]
  readonly attribute unsigned long focusOffset;
  readonly attribute boolean       isCollapsed;
  [ChromeOnly]
  readonly attribute boolean       areNormalAndCrossShadowBoundaryRangesCollapsed;

  [ChromeOnly]
  readonly attribute Node?         mayCrossShadowBoundaryFocusNode;

  /**
   * Returns the number of ranges in the selection.
   */
  readonly attribute unsigned long rangeCount;
  readonly attribute DOMString     type;
  readonly attribute DOMString direction;
  /**
   * Returns the range at the specified index.  Throws if the index is
   * out of range.
   */
  [Throws]
  Range     getRangeAt(unsigned long index);
  /**
   * Adds a range to the current selection.
   */
  [Throws, BinaryName="addRangeJS"]
  undefined addRange(Range range);
  /**
   * Removes a range from the current selection.
   */
  [Throws, BinaryName="removeRangeAndUnselectFramesAndNotifyListeners"]
  undefined removeRange(Range range);
  /**
   * Removes all ranges from the current selection.
   */
  [Throws]
  undefined removeAllRanges();
  [Throws, BinaryName="RemoveAllRanges"]
  undefined empty();

  [Pref="dom.shadowdom.selection_across_boundary_enabled"]
  sequence<StaticRange> getComposedRanges(optional (ShadowRoot or GetComposedRangesOptions) options = {}, ShadowRoot... shadowRoots);

  [Throws, BinaryName="collapseJS"]
  undefined collapse(Node? node, optional unsigned long offset = 0);
  [Throws, BinaryName="collapseJS"]
  undefined setPosition(Node? node, optional unsigned long offset = 0);
  [Throws, BinaryName="collapseToStartJS"]
  undefined collapseToStart();
  [Throws, BinaryName="collapseToEndJS"]
  undefined collapseToEnd();
  [Throws, BinaryName="extendJS"]
  undefined extend(Node node, optional unsigned long offset = 0);
  [Throws, BinaryName="setBaseAndExtentJS"]
  undefined setBaseAndExtent(Node anchorNode,
                             unsigned long anchorOffset,
                             Node focusNode,
                             unsigned long focusOffset);
  [Throws, BinaryName="selectAllChildrenJS"]
  undefined selectAllChildren(Node node);
  undefined modify(optional DOMString alter = "", optional DOMString direction = "",
                   optional DOMString granularity = "");
  [CEReactions, Throws]
  undefined deleteFromDocument();
  [Throws]
  boolean   containsNode(Node node,
                         optional boolean allowPartialContainment = false);
  [NeedsCallerType]
  stringifier DOMString ();
};

// Additional chrome-only methods.
interface nsISelectionListener;
partial interface Selection {
  /**
   * A true value means "selection after newline"; false means "selection before
   * newline" when a selection is positioned "between lines".
   */
  [ChromeOnly,Throws, BinaryName=interlinePositionJS]
  attribute boolean interlinePosition;

  [Throws]
  attribute short? caretBidiLevel;

  [ChromeOnly,Throws]
  DOMString  toStringWithFormat(DOMString formatType, unsigned long flags, long wrapColumn);
  [ChromeOnly]
  undefined  addSelectionListener(nsISelectionListener newListener);
  [ChromeOnly]
  undefined  removeSelectionListener(nsISelectionListener listenerToRemove);

  [ChromeOnly,BinaryName="rawType"]
  readonly attribute short selectionType;

  /**
   * Return array of ranges intersecting with the given DOM interval.
   */
  [ChromeOnly,Throws,Pref="dom.testing.selection.GetRangesForInterval"]
  sequence<Range> GetRangesForInterval(Node beginNode, long beginOffset, Node endNode, long endOffset,
                                       boolean allowAdjacent);

  /**
   * setColors() sets custom colors for the selection.
   * Currently, this is supported only when the selection type is SELECTION_FIND.
   * Otherwise, throws an exception.
   *
   * @param aForegroundColor     The foreground color of the selection.
   *                             If this is "currentColor", foreground color
   *                             isn't changed by this selection.
   * @param aBackgroundColor     The background color of the selection.
   *                             If this is "transparent", background color is
   *                             never painted.
   * @param aAltForegroundColor  The alternative foreground color of the
   *                             selection.
   *                             If aBackgroundColor doesn't have sufficient
   *                             contrast with its around or foreground color
   *                             if "currentColor" is specified, alternative
   *                             colors are used if it have higher contrast.
   * @param aAltBackgroundColor  The alternative background color of the
   *                             selection.
   */
  [ChromeOnly,Throws]
  undefined setColors(DOMString aForegroundColor, DOMString aBackgroundColor,
                      DOMString aAltForegroundColor, DOMString aAltBackgroundColor);

  /**
   * resetColors() forget the customized colors which were set by setColors().
   */
  [ChromeOnly]
  undefined resetColors();
};
