/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.toolbar

import androidx.navigation.NavController
import mozilla.components.browser.state.action.ContentAction
import mozilla.components.browser.state.action.ShareResourceAction
import mozilla.components.browser.state.ext.getUrl
import mozilla.components.browser.state.selector.findCustomTabOrSelectedTab
import mozilla.components.browser.state.selector.findTab
import mozilla.components.browser.state.selector.getNormalOrPrivateTabs
import mozilla.components.browser.state.selector.selectedTab
import mozilla.components.browser.state.state.SessionState
import mozilla.components.browser.state.state.content.ShareResourceState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.EngineView
import mozilla.components.concept.engine.prompt.ShareData
import mozilla.components.feature.tabs.TabsUseCases
import mozilla.components.support.ktx.kotlin.isContentUrl
import mozilla.components.support.ktx.kotlin.isUrl
import mozilla.components.ui.tabcounter.TabCounterMenu
import mozilla.telemetry.glean.private.NoExtras
import org.mozilla.fenix.GleanMetrics.Events
import org.mozilla.fenix.GleanMetrics.ReaderMode
import org.mozilla.fenix.GleanMetrics.Translations
import org.mozilla.fenix.HomeActivity
import org.mozilla.fenix.NavGraphDirections
import org.mozilla.fenix.R
import org.mozilla.fenix.browser.BrowserAnimator
import org.mozilla.fenix.browser.BrowserAnimator.Companion.getToolbarNavOptions
import org.mozilla.fenix.browser.BrowserFragmentDirections
import org.mozilla.fenix.browser.browsingmode.BrowsingMode
import org.mozilla.fenix.browser.readermode.ReaderModeController
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction.SnackbarAction
import org.mozilla.fenix.components.menu.MenuAccessPoint
import org.mozilla.fenix.components.toolbar.interactor.BrowserToolbarInteractor
import org.mozilla.fenix.components.usecases.FenixBrowserUseCases
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.nav
import org.mozilla.fenix.ext.navigateSafe
import org.mozilla.fenix.ext.settings
import org.mozilla.fenix.home.HomeScreenViewModel
import org.mozilla.fenix.utils.Settings

/**
 * An interface that handles the view manipulation of the BrowserToolbar, triggered by the Interactor
 */
interface BrowserToolbarController {
    fun handleScroll(offset: Int)
    fun handleToolbarPaste(text: String)
    fun handleToolbarPasteAndGo(text: String)
    fun handleToolbarClick()
    fun handleTabCounterClick()
    fun handleTabCounterItemInteraction(item: TabCounterMenu.Item)
    fun handleReaderModePressed(enabled: Boolean)

    /**
     * @see [BrowserToolbarInteractor.onHomeButtonClicked]
     */
    fun handleHomeButtonClick()

    /**
     * @see [BrowserToolbarInteractor.onTranslationsButtonClicked]
     */
    fun handleTranslationsButtonClick()

    /**
     * @see [BrowserToolbarInteractor.onShareActionClicked]
     */
    fun onShareActionClicked()

    /**
     * @see [BrowserToolbarInteractor.onNewTabButtonClicked]
     */
    fun handleNewTabButtonClick()

    /**
     * @see [BrowserToolbarInteractor.onNewTabButtonLongClicked]
     */
    fun handleNewTabButtonLongClick()

    /**
     * @see [BrowserToolbarInteractor.onMenuButtonClicked]
     */
    fun handleMenuButtonClicked(
        accessPoint: MenuAccessPoint,
        customTabSessionId: String? = null,
        isSandboxCustomTab: Boolean = false,
    )
}

@Suppress("LongParameterList")
class DefaultBrowserToolbarController(
    private val store: BrowserStore,
    private val appStore: AppStore,
    private val tabsUseCases: TabsUseCases,
    private val fenixBrowserUseCases: FenixBrowserUseCases,
    private val activity: HomeActivity,
    private val settings: Settings,
    private val navController: NavController,
    private val readerModeController: ReaderModeController,
    private val engineView: EngineView,
    private val homeViewModel: HomeScreenViewModel,
    private val customTabSessionId: String?,
    private val browserAnimator: BrowserAnimator,
    private val onTabCounterClicked: () -> Unit,
    private val onCloseTab: (SessionState) -> Unit,
) : BrowserToolbarController {

    private val currentSession
        get() = store.state.findCustomTabOrSelectedTab(customTabSessionId)

    override fun handleToolbarPaste(text: String) {
        navController.nav(
            R.id.browserFragment,
            BrowserFragmentDirections.actionGlobalSearchDialog(
                sessionId = currentSession?.id,
                pastedText = text,
            ),
            getToolbarNavOptions(activity),
        )
    }

    override fun handleToolbarPasteAndGo(text: String) {
        if (text.isUrl()) {
            store.updateSearchTermsOfSelectedSession("")
            activity.components.useCases.sessionUseCases.loadUrl(text)
            return
        }

        store.updateSearchTermsOfSelectedSession(text)
        activity.components.useCases.searchUseCases.defaultSearch.invoke(
            text,
            sessionId = store.state.selectedTabId,
        )
    }

    override fun handleToolbarClick() {
        Events.searchBarTapped.record(Events.SearchBarTappedExtra("BROWSER"))
        // If we're displaying awesomebar search results, Home screen will not be visible (it's
        // covered up with the search results). So, skip the navigation event in that case.
        // If we don't, there's a visual flickr as we navigate to Home and then display search
        // results on top it.
        if (currentSession?.content?.searchTerms.isNullOrBlank()) {
            browserAnimator.captureEngineViewAndDrawStatically {
                navController.navigate(
                    BrowserFragmentDirections.actionGlobalHome(),
                )
                navController.navigate(
                    BrowserFragmentDirections.actionGlobalSearchDialog(
                        currentSession?.id,
                    ),
                    getToolbarNavOptions(activity),
                )
            }
        } else {
            navController.navigate(
                BrowserFragmentDirections.actionGlobalSearchDialog(
                    currentSession?.id,
                ),
                getToolbarNavOptions(activity),
            )
        }
    }

    override fun handleTabCounterClick() {
        onTabCounterClicked.invoke()
    }

    override fun handleReaderModePressed(enabled: Boolean) {
        if (enabled) {
            readerModeController.showReaderView()
            ReaderMode.opened.record(NoExtras())
        } else {
            readerModeController.hideReaderView()
            ReaderMode.closed.record(NoExtras())
        }
    }

    override fun handleTabCounterItemInteraction(item: TabCounterMenu.Item) {
        when (item) {
            is TabCounterMenu.Item.CloseTab -> {
                store.state.selectedTab?.let {
                    // When closing the last tab we must show the undo snackbar in the home fragment
                    if (store.state.getNormalOrPrivateTabs(it.content.private).count() == 1) {
                        homeViewModel.sessionToDelete = it.id
                        navController.navigate(
                            BrowserFragmentDirections.actionGlobalHome(),
                        )
                    } else {
                        onCloseTab.invoke(it)
                        tabsUseCases.removeTab(it.id, selectParentIfExists = true)
                    }
                }
            }
            is TabCounterMenu.Item.NewTab -> {
                activity.browsingModeManager.mode = BrowsingMode.Normal
                navController.navigate(
                    BrowserFragmentDirections.actionGlobalHome(focusOnAddressBar = true),
                )
            }
            is TabCounterMenu.Item.NewPrivateTab -> {
                activity.browsingModeManager.mode = BrowsingMode.Private
                navController.navigate(
                    BrowserFragmentDirections.actionGlobalHome(focusOnAddressBar = true),
                )
            }
        }
    }

    override fun handleScroll(offset: Int) {
        if (activity.settings().isDynamicToolbarEnabled) {
            engineView.setVerticalClipping(offset)
        }
    }

    override fun handleHomeButtonClick() {
        Events.browserToolbarHomeTapped.record(NoExtras())

        if (settings.enableHomepageAsNewTab) {
            fenixBrowserUseCases.navigateToHomepage()
        } else {
            browserAnimator.captureEngineViewAndDrawStatically {
                navController.navigate(
                    BrowserFragmentDirections.actionGlobalHome(),
                )
            }
        }
    }

    override fun handleTranslationsButtonClick() {
        Translations.action.record(Translations.ActionExtra("main_flow_toolbar"))

        appStore.dispatch(SnackbarAction.SnackbarDismissed)

        val directions =
            BrowserFragmentDirections.actionBrowserFragmentToTranslationsDialogFragment()
        navController.navigateSafe(R.id.browserFragment, directions)
    }

    override fun onShareActionClicked() {
        val sessionId = currentSession?.id
        val url = sessionId?.let {
            store.state.findTab(it)?.getUrl()
        }

        if (url?.isContentUrl() == true) {
            val tab = store.state.findTab(sessionId) ?: return

            store.dispatch(
                ShareResourceAction.AddShareAction(
                    tab.id,
                    ShareResourceState.LocalResource(url),
                ),
            )
        } else {
            val directions = NavGraphDirections.actionGlobalShareFragment(
                sessionId = sessionId,
                data = arrayOf(
                    ShareData(
                        url = url,
                        title = currentSession?.content?.title,
                    ),
                ),
                showPage = true,
            )
            navController.navigate(directions)
        }
    }

    override fun handleNewTabButtonClick() {
        if (settings.enableHomepageAsNewTab) {
            fenixBrowserUseCases.addNewHomepageTab(
                private = currentSession?.content?.private ?: false,
            )
        }

        Events.browserToolbarAction.record(Events.BrowserToolbarActionExtra("new_tab"))

        browserAnimator.captureEngineViewAndDrawStatically {
            navController.navigate(
                BrowserFragmentDirections.actionGlobalHome(focusOnAddressBar = true),
            )
        }
    }

    override fun handleNewTabButtonLongClick() {
        Events.browserToolbarAction.record(Events.BrowserToolbarActionExtra("new_tab_long_press"))
    }

    override fun handleMenuButtonClicked(
        accessPoint: MenuAccessPoint,
        customTabSessionId: String?,
        isSandboxCustomTab: Boolean,
    ) {
        navController.navigate(
            BrowserFragmentDirections.actionGlobalMenuDialogFragment(
                accesspoint = accessPoint,
                customTabSessionId = customTabSessionId,
                isSandboxCustomTab = isSandboxCustomTab,
            ),
        )
    }

    companion object {
        internal const val TELEMETRY_BROWSER_IDENTIFIER = "browserMenu"
    }
}

private fun BrowserStore.updateSearchTermsOfSelectedSession(
    searchTerms: String,
) {
    val selectedTabId = state.selectedTabId ?: return

    dispatch(
        ContentAction.UpdateSearchTermsAction(
            selectedTabId,
            searchTerms,
        ),
    )
}
