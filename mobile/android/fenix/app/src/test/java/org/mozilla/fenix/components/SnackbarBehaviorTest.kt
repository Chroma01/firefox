/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components

import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.FrameLayout
import androidx.coordinatorlayout.widget.CoordinatorLayout
import io.mockk.every
import io.mockk.mockk
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.R
import org.mozilla.fenix.components.toolbar.ToolbarPosition
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class SnackbarBehaviorTest {
    private val snackbarContainer = mockk<FrameLayout>(relaxed = true)
    private var snackbarLayoutParams = CoordinatorLayout.LayoutParams(0, 0)
    private val dependency = View(testContext)
    private val parent = CoordinatorLayout(testContext)

    @Before
    fun setup() {
        every { snackbarContainer.layoutParams } returns snackbarLayoutParams
        every { snackbarContainer.post(any()) } answers {
            // Immediately run the given Runnable argument
            val action: Runnable = firstArg()
            action.run()
            true
        }
        parent.addView(dependency)
    }

    @Test
    fun `GIVEN no valid anchors are shown WHEN the snackbar is shown THEN don't anchor it`() {
        val behavior = SnackbarBehavior<ViewGroup>(testContext, ToolbarPosition.BOTTOM, false)

        behavior.layoutDependsOn(parent, snackbarContainer, dependency)

        assertSnackbarIsPlacedAtTheBottomOfTheScreen()
    }

    @Test
    fun `GIVEN a toolbar shown at top WHEN the snackbar is shown THEN don't anchor it`() {
        dependency.id = R.id.toolbar
        val behavior = SnackbarBehavior<ViewGroup>(testContext, ToolbarPosition.TOP, false)

        behavior.layoutDependsOn(parent, snackbarContainer, dependency)

        assertSnackbarIsPlacedAtTheBottomOfTheScreen()
    }

    @Test
    fun `GIVEN a toolbar layout shown at top WHEN the snackbar is shown THEN don't anchor it`() {
        dependency.id = R.id.toolbarLayout
        val behavior = SnackbarBehavior<ViewGroup>(testContext, ToolbarPosition.TOP, false)

        behavior.layoutDependsOn(parent, snackbarContainer, dependency)

        assertSnackbarIsPlacedAtTheBottomOfTheScreen()
    }

    @Test
    fun `GIVEN a bottom toolbar is shown WHEN the snackbar is shown THEN place the snackbar above the toolbar`() {
        dependency.id = R.id.toolbar
        val behavior = SnackbarBehavior<ViewGroup>(testContext, ToolbarPosition.BOTTOM, false)

        behavior.layoutDependsOn(parent, snackbarContainer, dependency)

        assertSnackbarPlacementAboveAnchor()
    }

    @Test
    fun `GIVEN a bottom composable toolbar is shown WHEN the snackbar is shown THEN place the snackbar above the composable toolbar`() {
        dependency.id = R.id.composable_toolbar
        val behavior = SnackbarBehavior<ViewGroup>(testContext, ToolbarPosition.BOTTOM, false)

        behavior.layoutDependsOn(parent, snackbarContainer, dependency)

        assertSnackbarPlacementAboveAnchor()
    }

    @Test
    fun `GIVEN a top composable toolbar is shown WHEN the snackbar is shown THEN place the snackbar at the bottom of the screen`() {
        dependency.id = R.id.composable_toolbar
        val behavior = SnackbarBehavior<ViewGroup>(testContext, ToolbarPosition.TOP, false)

        behavior.layoutDependsOn(parent, snackbarContainer, dependency)

        assertSnackbarIsPlacedAtTheBottomOfTheScreen()
    }

    @Test
    fun `GIVEN the snackbar is anchored to the bottom toolbar WHEN the toolbar is not shown anymore THEN place the snackbar at the bottom`() {
        val toolbar = View(testContext)
            .apply { id = R.id.toolbar }
            .also { parent.addView(it) }
        val behavior = SnackbarBehavior<ViewGroup>(testContext, ToolbarPosition.BOTTOM, false)

        // Test the scenario where the toolbar is invisible.
        behavior.layoutDependsOn(parent, snackbarContainer, dependency)
        assertSnackbarPlacementAboveAnchor(toolbar)
        toolbar.visibility = View.GONE
        behavior.layoutDependsOn(parent, snackbarContainer, dependency)
        assertSnackbarIsPlacedAtTheBottomOfTheScreen()

        // Test the scenario where the toolbar is removed from parent.
        toolbar.visibility = View.VISIBLE
        behavior.layoutDependsOn(parent, snackbarContainer, dependency)
        assertSnackbarPlacementAboveAnchor(toolbar)
        parent.removeView(toolbar)
        behavior.layoutDependsOn(parent, snackbarContainer, dependency)
        assertSnackbarIsPlacedAtTheBottomOfTheScreen()
    }

    @Test
    fun `GIVEN the snackbar is anchored to the logins bar and a top toolbar is shown WHEN the logins bar is not shown anymore THEN place the snackbar to the bottom`() {
        val loginsBar = View(testContext)
            .apply { id = R.id.loginSelectBar }
            .also { parent.addView(it) }
        View(testContext)
            .apply { id = R.id.toolbar }
            .also { parent.addView(it) }
        val behavior = SnackbarBehavior<ViewGroup>(testContext, ToolbarPosition.TOP, false)

        behavior.layoutDependsOn(parent, snackbarContainer, dependency)
        assertSnackbarPlacementAboveAnchor(loginsBar)

        loginsBar.visibility = View.GONE
        behavior.layoutDependsOn(parent, snackbarContainer, dependency)
        assertSnackbarIsPlacedAtTheBottomOfTheScreen()
    }

    @Test
    fun `GIVEN the snackbar is anchored to the logins bar and navigation bar is shown WHEN the logins bar is not shown anymore THEN place the snackbar to the navigation bar`() {
        val loginsBar = View(testContext)
            .apply { id = R.id.loginSelectBar }
            .also { parent.addView(it) }
        val toolbar = View(testContext)
            .apply { id = R.id.toolbar }
            .also { parent.addView(it) }
        val behavior = SnackbarBehavior<ViewGroup>(testContext, ToolbarPosition.TOP, true)

        behavior.layoutDependsOn(parent, snackbarContainer, dependency)
        assertSnackbarPlacementAboveAnchor(loginsBar)

        loginsBar.visibility = View.GONE
        behavior.layoutDependsOn(parent, snackbarContainer, dependency)
        assertSnackbarPlacementAboveAnchor(toolbar)
    }

    @Test
    fun `GIVEN the snackbar is anchored to the password bar and a top toolbar is shown WHEN the password bar is not shown anymore THEN place the snackbar to the bottom`() {
        val passwordBar = View(testContext)
            .apply { id = R.id.suggestStrongPasswordBar }
            .also { parent.addView(it) }
        View(testContext)
            .apply { id = R.id.toolbar }
            .also { parent.addView(it) }
        val behavior = SnackbarBehavior<ViewGroup>(testContext, ToolbarPosition.TOP, false)

        behavior.layoutDependsOn(parent, snackbarContainer, dependency)
        assertSnackbarPlacementAboveAnchor(passwordBar)

        passwordBar.visibility = View.GONE
        behavior.layoutDependsOn(parent, snackbarContainer, dependency)
        assertSnackbarIsPlacedAtTheBottomOfTheScreen()
    }

    @Test
    fun `GIVEN the snackbar is anchored to the password bar and navigation bar is shown WHEN the password bar is not shown anymore THEN place the snackbar to the navigation bar`() {
        val passwordBar = View(testContext)
            .apply { id = R.id.suggestStrongPasswordBar }
            .also { parent.addView(it) }
        val toolbar = View(testContext)
            .apply { id = R.id.toolbar }
            .also { parent.addView(it) }
        val behavior = SnackbarBehavior<ViewGroup>(testContext, ToolbarPosition.TOP, true)

        behavior.layoutDependsOn(parent, snackbarContainer, dependency)
        assertSnackbarPlacementAboveAnchor(passwordBar)

        passwordBar.visibility = View.GONE
        behavior.layoutDependsOn(parent, snackbarContainer, dependency)
        assertSnackbarPlacementAboveAnchor(toolbar)
    }

    @Test
    fun `GIVEN the snackbar is anchored to the logins bar and a bottom toolbar is shown WHEN the logins bar is not shown anymore THEN place the snackbar above the toolbar`() {
        val loginsBar = View(testContext)
            .apply { id = R.id.loginSelectBar }
            .also { parent.addView(it) }
        val toolbar = View(testContext)
            .apply { id = R.id.toolbar }
            .also { parent.addView(it) }
        val behavior = SnackbarBehavior<ViewGroup>(testContext, ToolbarPosition.BOTTOM, false)

        behavior.layoutDependsOn(parent, snackbarContainer, dependency)
        assertSnackbarPlacementAboveAnchor(loginsBar)

        loginsBar.visibility = View.GONE
        behavior.layoutDependsOn(parent, snackbarContainer, dependency)
        assertSnackbarPlacementAboveAnchor(toolbar)
    }

    @Test
    fun `GIVEN the snackbar is anchored to the password bar and a bottom toolbar is shown WHEN the password bar is not shown anymore THEN place the snackbar above the toolbar`() {
        val passwordBar = View(testContext)
            .apply { id = R.id.suggestStrongPasswordBar }
            .also { parent.addView(it) }
        val toolbar = View(testContext)
            .apply { id = R.id.toolbar }
            .also { parent.addView(it) }
        val behavior = SnackbarBehavior<ViewGroup>(testContext, ToolbarPosition.BOTTOM, false)

        behavior.layoutDependsOn(parent, snackbarContainer, dependency)
        assertSnackbarPlacementAboveAnchor(passwordBar)

        passwordBar.visibility = View.GONE
        behavior.layoutDependsOn(parent, snackbarContainer, dependency)
        assertSnackbarPlacementAboveAnchor(toolbar)
    }

    @Test
    fun `GIVEN the snackbar is anchored based on a top toolbar WHEN the toolbar is not shown anymore THEN place the snackbar at the bottom`() {
        val toolbar = View(testContext)
            .apply { id = R.id.toolbar }
            .also { parent.addView(it) }
        val behavior = SnackbarBehavior<ViewGroup>(testContext, ToolbarPosition.TOP, false)

        // Test the scenario where the toolbar is invisible.
        behavior.layoutDependsOn(parent, snackbarContainer, dependency)
        assertSnackbarIsPlacedAtTheBottomOfTheScreen()
        toolbar.visibility = View.GONE
        behavior.layoutDependsOn(parent, snackbarContainer, dependency)
        assertSnackbarIsPlacedAtTheBottomOfTheScreen()

        // Test the scenario where the toolbar is removed from parent.
        toolbar.visibility = View.VISIBLE
        behavior.layoutDependsOn(parent, snackbarContainer, dependency)
        assertSnackbarIsPlacedAtTheBottomOfTheScreen()
        parent.removeView(toolbar)
        behavior.layoutDependsOn(parent, snackbarContainer, dependency)
        assertSnackbarIsPlacedAtTheBottomOfTheScreen()
    }

    private fun assertSnackbarPlacementAboveAnchor(anchor: View = dependency) {
        assertEquals(anchor.id, snackbarLayoutParams.anchorId)
        assertEquals(Gravity.TOP or Gravity.CENTER_HORIZONTAL, snackbarLayoutParams.anchorGravity)
        assertEquals(Gravity.TOP or Gravity.CENTER_HORIZONTAL, snackbarLayoutParams.gravity)
    }

    private fun assertSnackbarIsPlacedAtTheBottomOfTheScreen() {
        assertEquals(View.NO_ID, snackbarLayoutParams.anchorId)
        assertEquals(Gravity.NO_GRAVITY, snackbarLayoutParams.anchorGravity)
        assertEquals(Gravity.BOTTOM or Gravity.CENTER_HORIZONTAL, snackbarLayoutParams.gravity)
    }
}
