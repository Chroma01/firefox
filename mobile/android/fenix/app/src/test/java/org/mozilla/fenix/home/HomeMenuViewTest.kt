/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home

import android.view.View
import androidx.lifecycle.LifecycleOwner
import androidx.navigation.NavController
import androidx.navigation.NavDirections
import androidx.navigation.NavOptions
import io.mockk.every
import io.mockk.just
import io.mockk.mockk
import io.mockk.runs
import io.mockk.spyk
import io.mockk.verify
import mozilla.appservices.places.BookmarkRoot
import mozilla.components.browser.menu.view.MenuButton
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.GleanMetrics.Events
import org.mozilla.fenix.GleanMetrics.HomeScreen
import org.mozilla.fenix.HomeActivity
import org.mozilla.fenix.R
import org.mozilla.fenix.components.accounts.AccountState
import org.mozilla.fenix.components.accounts.FenixFxAEntryPoint
import org.mozilla.fenix.components.usecases.FenixBrowserUseCases
import org.mozilla.fenix.helpers.FenixGleanTestRule
import org.mozilla.fenix.settings.SupportUtils
import org.mozilla.fenix.whatsnew.WhatsNew
import org.robolectric.RobolectricTestRunner
import java.lang.ref.WeakReference
import org.mozilla.fenix.GleanMetrics.HomeMenu as HomeMenuMetrics

@RunWith(RobolectricTestRunner::class)
class HomeMenuViewTest {

    @get:Rule
    val gleanTestRule = FenixGleanTestRule(testContext)

    private lateinit var view: View
    private lateinit var lifecycleOwner: LifecycleOwner
    private lateinit var homeActivity: HomeActivity
    private lateinit var navController: NavController
    private lateinit var menuButton: MenuButton
    private lateinit var fenixBrowserUseCases: FenixBrowserUseCases
    private lateinit var homeMenuView: HomeMenuView

    @Before
    fun setup() {
        view = mockk(relaxed = true)
        lifecycleOwner = mockk(relaxed = true)
        homeActivity = mockk(relaxed = true)
        navController = mockk(relaxed = true) {
            every { navigate(any<NavDirections>(), any<NavOptions>()) } just runs
            every { currentDestination?.id } returns R.id.homeFragment
        }
        fenixBrowserUseCases = mockk(relaxed = true)

        menuButton = spyk(MenuButton(testContext))

        homeMenuView = HomeMenuView(
            context = testContext,
            lifecycleOwner = lifecycleOwner,
            homeActivity = homeActivity,
            navController = navController,
            fenixBrowserUseCases = fenixBrowserUseCases,
            menuButton = WeakReference(menuButton),
        )
    }

    @Test
    fun `WHEN dismiss menu is called THEN the menu is dismissed`() {
        homeMenuView.dismissMenu()

        verify {
            menuButton.dismissMenu()
        }
    }

    @Test
    fun `WHEN Settings menu item is tapped THEN navigate to settings fragment and record metrics`() {
        assertNull(HomeMenuMetrics.settingsItemClicked.testGetValue())

        homeMenuView.onItemTapped(HomeMenu.Item.Settings)

        assertNotNull(HomeMenuMetrics.settingsItemClicked.testGetValue())

        verify {
            navController.navigate(
                HomeFragmentDirections.actionGlobalSettingsFragment(),
                null,
            )
        }
    }

    @Test
    fun `WHEN Customize Home menu item is tapped THEN navigate to home settings fragment and record metrics`() {
        assertNull(HomeScreen.customizeHomeClicked.testGetValue())

        homeMenuView.onItemTapped(HomeMenu.Item.CustomizeHome)

        assertNotNull(HomeScreen.customizeHomeClicked.testGetValue())

        verify {
            navController.navigate(
                HomeFragmentDirections.actionGlobalHomeSettingsFragment(),
                null,
            )
        }
    }

    @Test
    fun `GIVEN various sync account state WHEN Sync Account menu item is tapped THEN navigate to the appropriate sync fragment`() {
        homeMenuView.onItemTapped(HomeMenu.Item.SyncAccount(AccountState.AUTHENTICATED))

        verify {
            navController.navigate(
                HomeFragmentDirections.actionGlobalAccountSettingsFragment(),
                null,
            )
        }

        homeMenuView.onItemTapped(HomeMenu.Item.SyncAccount(AccountState.NEEDS_REAUTHENTICATION))

        verify {
            navController.navigate(
                HomeFragmentDirections.actionGlobalAccountProblemFragment(
                    entrypoint = FenixFxAEntryPoint.HomeMenu,
                ),
                null,
            )
        }

        homeMenuView.onItemTapped(HomeMenu.Item.SyncAccount(AccountState.NO_ACCOUNT))

        verify {
            navController.navigate(
                HomeFragmentDirections.actionGlobalTurnOnSync(entrypoint = FenixFxAEntryPoint.HomeMenu),
                null,
            )
        }
    }

    @Test
    fun `WHEN Bookmarks menu item is tapped THEN navigate to the bookmarks fragment`() {
        homeMenuView.onItemTapped(HomeMenu.Item.Bookmarks)

        verify {
            navController.navigate(
                HomeFragmentDirections.actionGlobalBookmarkFragment(BookmarkRoot.Mobile.id),
                null,
            )
        }
    }

    @Test
    fun `WHEN History menu item is tapped THEN navigate to the history fragment`() {
        homeMenuView.onItemTapped(HomeMenu.Item.History)

        verify {
            navController.navigate(
                HomeFragmentDirections.actionGlobalHistoryFragment(),
                null,
            )
        }
    }

    @Test
    fun `WHEN Downloads menu item is tapped THEN navigate to the downloads fragment`() {
        homeMenuView.onItemTapped(HomeMenu.Item.Downloads)

        verify {
            navController.navigate(
                HomeFragmentDirections.actionGlobalDownloadsFragment(),
                null,
            )
        }
    }

    @Test
    fun `WHEN Help menu item is tapped THEN open the browser to the SUMO help page  and record metrics`() {
        assertNull(HomeMenuMetrics.helpTapped.testGetValue())

        homeMenuView.onItemTapped(HomeMenu.Item.Help)

        assertNotNull(HomeMenuMetrics.helpTapped.testGetValue())

        verify {
            navController.navigate(
                HomeFragmentDirections.actionGlobalBrowser(),
                null,
            )
            fenixBrowserUseCases.loadUrlOrSearch(
                searchTermOrURL = SupportUtils.getSumoURLForTopic(
                    context = testContext,
                    topic = SupportUtils.SumoTopic.HELP,
                ),
                newTab = true,
                private = false,
            )
        }
    }

    @Test
    fun `WHEN Whats New menu item is tapped THEN open the browser to the SUMO whats new page and record metrics`() {
        assertNull(Events.whatsNewTapped.testGetValue())

        homeMenuView.onItemTapped(HomeMenu.Item.WhatsNew)

        assertNotNull(Events.whatsNewTapped.testGetValue())

        val snapshot = Events.whatsNewTapped.testGetValue()!!

        assertEquals(1, snapshot.size)
        assertEquals("HOME", snapshot.single().extra?.getValue("source"))

        verify {
            WhatsNew.userViewedWhatsNew(testContext)

            navController.navigate(
                HomeFragmentDirections.actionGlobalBrowser(),
                null,
            )
            fenixBrowserUseCases.loadUrlOrSearch(
                searchTermOrURL = SupportUtils.WHATS_NEW_URL,
                newTab = true,
                private = false,
            )
        }
    }

    @Test
    fun `WHEN Reconnect Sync menu item is tapped THEN navigate to the account problem fragment`() {
        homeMenuView.onItemTapped(HomeMenu.Item.ReconnectSync)

        verify {
            navController.navigate(
                HomeFragmentDirections.actionGlobalAccountProblemFragment(
                    entrypoint = FenixFxAEntryPoint.HomeMenu,
                ),
                null,
            )
        }
    }

    @Test
    fun `WHEN Extensions menu item is tapped THEN navigate to the addons management fragment`() {
        homeMenuView.onItemTapped(HomeMenu.Item.Extensions)

        verify {
            navController.navigate(
                HomeFragmentDirections.actionGlobalAddonsManagementFragment(),
                null,
            )
        }
    }
}
