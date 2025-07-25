/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.awesomebar.provider

import androidx.core.net.toUri
import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlinx.coroutines.test.runTest
import mozilla.components.browser.state.action.TabListAction
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.feature.tabs.TabsUseCases
import mozilla.components.support.ktx.android.net.sameHostWithoutMobileSubdomainAs
import mozilla.components.support.test.mock
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mockito.never
import org.mockito.Mockito.verify

@RunWith(AndroidJUnit4::class)
class SessionSuggestionProviderTest {
    @Test
    fun `Provider returns empty list when text is empty`() = runTest {
        val provider = SessionSuggestionProvider(mock(), mock(), switchToTabDescription = "Switch to tab")

        val suggestions = provider.onInputChanged("")
        assertTrue(suggestions.isEmpty())
    }

    @Test
    fun `Provider returns Sessions with matching URLs`() = runTest {
        val store = BrowserStore()

        val tab1 = createTab("https://www.mozilla.org")
        val tab2 = createTab("https://example.com")
        val tab3 = createTab("https://firefox.com")
        val tab4 = createTab("https://example.org/")

        val provider = SessionSuggestionProvider(store, mock(), switchToTabDescription = "Switch to tab")

        run {
            val suggestions = provider.onInputChanged("Example")
            assertTrue(suggestions.isEmpty())
        }

        store.dispatch(TabListAction.AddTabAction(tab1)).join()
        store.dispatch(TabListAction.AddTabAction(tab2)).join()
        store.dispatch(TabListAction.AddTabAction(tab3)).join()

        run {
            val suggestions = provider.onInputChanged("Example")
            assertEquals(1, suggestions.size)

            assertEquals(tab2.id, suggestions[0].id)
            assertEquals("Switch to tab", suggestions[0].description)
        }

        store.dispatch(TabListAction.AddTabAction(tab4)).join()

        run {
            val suggestions = provider.onInputChanged("Example")
            assertEquals(2, suggestions.size)

            assertEquals(tab2.id, suggestions[0].id)
            assertEquals(tab4.id, suggestions[1].id)
            assertEquals("Switch to tab", suggestions[0].description)
            assertEquals("Switch to tab", suggestions[1].description)
        }
    }

    @Test
    fun `WHEN input text has trailing space THEN Provider returns Sessions with matching URLs`() =
        runTest {
            val store = BrowserStore()

            val tab1 = createTab("https://www.mozilla.org")
            val tab2 = createTab("https://www.mozilla.org/example/of/content")

            val provider = SessionSuggestionProvider(store, mock(), switchToTabDescription = "Switch to tab")
            store.dispatch(TabListAction.AddTabAction(tab1)).join()
            store.dispatch(TabListAction.AddTabAction(tab2)).join()

            run {
                val suggestions = provider.onInputChanged("mozilla ")
                assertEquals(2, suggestions.size)

                assertEquals(tab1.id, suggestions[0].id)
                assertEquals("Switch to tab", suggestions[0].description)

                assertEquals(tab2.id, suggestions[1].id)
                assertEquals("Switch to tab", suggestions[1].description)
            }
        }

    @Test
    fun `WHEN input text has leading space THEN Provider returns Sessions with matching URLs`() =
        runTest {
            val store = BrowserStore()

            val tab1 = createTab("https://www.mozilla.org")
            val tab2 = createTab("https://www.mozilla.org/example/of/content")

            val provider = SessionSuggestionProvider(store, mock(), switchToTabDescription = "Switch to tab")
            store.dispatch(TabListAction.AddTabAction(tab1)).join()
            store.dispatch(TabListAction.AddTabAction(tab2)).join()

            run {
                val suggestions = provider.onInputChanged(" mozilla")
                assertEquals(2, suggestions.size)

                assertEquals(tab1.id, suggestions[0].id)
                assertEquals("Switch to tab", suggestions[0].description)

                assertEquals(tab2.id, suggestions[1].id)
                assertEquals("Switch to tab", suggestions[1].description)
            }
        }

    @Test
    fun `GIVEN input text has multiple matching words WHEN all match THEN Provider returns Sessions with matching URLs`() =
        runTest {
            val store = BrowserStore()

            val tab1 = createTab("https://www.mozilla.org/example/of/content")

            val provider = SessionSuggestionProvider(store, mock(), switchToTabDescription = "Switch to tab")
            store.dispatch(TabListAction.AddTabAction(tab1)).join()

            run {
                val suggestions = provider.onInputChanged("mozilla example content")
                assertEquals(1, suggestions.size)

                assertEquals(tab1.id, suggestions[0].id)
                assertEquals("Switch to tab", suggestions[0].description)
            }
        }

    @Test
    fun `GIVEN input text has multiple matching words WHEN some match THEN Provider returns an empty list`() =
        runTest {
            val store = BrowserStore()

            val tab1 = createTab("https://www.mozilla.org/example/of/content")

            val provider = SessionSuggestionProvider(store, mock(), switchToTabDescription = "Switch to tab")
            store.dispatch(TabListAction.AddTabAction(tab1)).join()

            run {
                val suggestions = provider.onInputChanged("mozilla example test")
                assertTrue(suggestions.isEmpty())
            }
        }

    @Test
    fun `Provider returns Sessions with matching titles`() = runTest {
        val tab1 = createTab("https://allizom.org", title = "Internet for people, not profit — Mozilla")
        val tab2 = createTab("https://getpocket.com", title = "Pocket: My List")
        val tab3 = createTab("https://firefox.com", title = "Download Firefox — Free Web Browser")

        val store = BrowserStore(
            BrowserState(
                tabs = listOf(tab1, tab2, tab3),
            ),
        )

        val provider = SessionSuggestionProvider(store, mock(), switchToTabDescription = "Switch to tab")

        run {
            val suggestions = provider.onInputChanged("Browser")
            assertEquals(1, suggestions.size)

            assertEquals(suggestions.first().id, tab3.id)
            assertEquals("Switch to tab", suggestions.first().description)
            assertEquals("Download Firefox — Free Web Browser", suggestions[0].title)
        }

        run {
            val suggestions = provider.onInputChanged("Mozilla")
            assertEquals(1, suggestions.size)

            assertEquals(tab1.id, suggestions.first().id)
            assertEquals("Switch to tab", suggestions.first().description)
            assertEquals("Internet for people, not profit — Mozilla", suggestions[0].title)
        }
    }

    @Test
    fun `Provider only returns non-private Sessions`() = runTest {
        val tab = createTab("https://www.mozilla.org")
        val privateTab1 = createTab("https://mozilla.org/firefox", private = true)
        val privateTab2 = createTab("https://mozilla.org/projects", private = true)

        val store = BrowserStore(
            BrowserState(
                tabs = listOf(tab, privateTab1, privateTab2),
            ),
        )

        val useCase: TabsUseCases.SelectTabUseCase = mock()

        val provider =
            SessionSuggestionProvider(store, useCase, switchToTabDescription = "Switch to tab")
        val suggestions = provider.onInputChanged("mozilla")

        assertEquals(1, suggestions.size)
    }

    @Test
    fun `Clicking suggestion invokes SelectTabUseCase`() = runTest {
        val tab = createTab("https://www.mozilla.org")

        val store = BrowserStore(
            BrowserState(
                tabs = listOf(tab),
            ),
        )

        val useCase: TabsUseCases.SelectTabUseCase = mock()

        val provider = SessionSuggestionProvider(store, useCase, switchToTabDescription = "Switch to tab")
        val suggestions = provider.onInputChanged("mozilla")
        assertEquals(1, suggestions.size)

        val suggestion = suggestions[0]

        verify(useCase, never()).invoke(tab.id)

        suggestion.onSuggestionClicked!!.invoke()

        verify(useCase).invoke(tab.id)
    }

    @Test
    fun `When excludeSelectedSession is true provider should not include the selected session`() = runTest {
        val store = BrowserStore(
            BrowserState(
                tabs = listOf(
                    createTab(id = "a", url = "https://wikipedia.org"),
                    createTab(id = "b", url = "https://www.mozilla.org"),
                ),
                selectedTabId = "b",
            ),
        )

        val useCase: TabsUseCases.SelectTabUseCase = mock()

        val provider = SessionSuggestionProvider(
            store, useCase, excludeSelectedSession = true,
            switchToTabDescription = "Switch to tab",
        )
        val suggestions = provider.onInputChanged("org")

        assertEquals(1, suggestions.size)
        assertEquals("a", suggestions.first().id)
        assertEquals("Switch to tab", suggestions.first().description)
    }

    @Test
    fun `When excludeSelectedSession is false provider should include the selected session`() = runTest {
        val store = BrowserStore(
            BrowserState(
                tabs = listOf(
                    createTab(id = "a", url = "https://wikipedia.org"),
                    createTab(id = "b", url = "https://www.mozilla.org"),
                ),
                selectedTabId = "b",
            ),
        )

        val useCase: TabsUseCases.SelectTabUseCase = mock()

        val provider = SessionSuggestionProvider(
            store,
            useCase,
            excludeSelectedSession = false,
            switchToTabDescription = "Switch to tab",
        )
        val suggestions = provider.onInputChanged("mozilla")

        assertEquals(1, suggestions.size)
        assertEquals("b", suggestions.first().id)
        assertEquals("Switch to tab", suggestions.first().description)
    }

    @Test
    fun `Uses title for chip title when available, but falls back to URL`() = runTest {
        val store = BrowserStore(
            BrowserState(
                tabs = listOf(
                    createTab(id = "a", url = "https://wikipedia.org", title = "Wikipedia"),
                    createTab(id = "b", url = "https://www.mozilla.org", title = ""),
                ),
                selectedTabId = "b",
            ),
        )

        val useCase: TabsUseCases.SelectTabUseCase = mock()

        val provider = SessionSuggestionProvider(
            store,
            useCase,
            excludeSelectedSession = false,
            switchToTabDescription = "Switch to tab",
        )
        var suggestions = provider.onInputChanged("mozilla")

        assertEquals(1, suggestions.size)
        assertEquals("b", suggestions.first().id)
        assertEquals("https://www.mozilla.org", suggestions.first().title)
        assertEquals("Switch to tab", suggestions.first().description)

        suggestions = provider.onInputChanged("wiki")
        assertEquals(1, suggestions.size)
        assertEquals("a", suggestions.first().id)
        assertEquals("Wikipedia", suggestions.first().title)
        assertEquals("Switch to tab", suggestions.first().description)
    }

    @Test
    fun `GIVEN a results uri filter WHEN querying tabs THEN return only the results that pass through the filter`() = runTest {
        val store = BrowserStore(
            BrowserState(
                tabs = listOf(
                    createTab(id = "a", url = "https://wikipedia.org"),
                    createTab(id = "b", url = "https://mozilla.org/firefox"),
                    createTab(id = "c", url = "https://mozilla.org/focus"),
                    createTab(id = "d", url = "https://www.mozilla.org/vpn"),
                ),
                selectedTabId = "d",
            ),
        )

        val provider = SessionSuggestionProvider(
            store = store,
            selectTabUseCase = mock(),
            resultsUriFilter = {
                it.sameHostWithoutMobileSubdomainAs("https://mozilla.org".toUri())
            },
            switchToTabDescription = "Switch to tab",
        )

        val suggestions = provider.onInputChanged("moz")

        assertEquals(3, suggestions.size)
        assertTrue(suggestions.map { it.title }.contains("https://mozilla.org/firefox"))
        assertTrue(suggestions.map { it.title }.contains("https://mozilla.org/firefox"))
        assertTrue(suggestions.map { it.title }.contains("https://www.mozilla.org/vpn"))
    }

    @Test
    fun `GIVEN a results uri filter WHEN querying tabs THEN return results containing mobile subdomains`() = runTest {
        val store = BrowserStore(
            BrowserState(
                tabs = listOf(
                    createTab(id = "a", url = "https://wikipedia.org"),
                    createTab(id = "b", url = "https://mozilla.org/firefox"),
                    createTab(id = "c", url = "https://mozilla.org/focus"),
                    createTab(id = "d", url = "https://www.mozilla.org/vpn"),
                    createTab(id = "e", url = "https://www.m.mozilla.org"),
                    createTab(id = "f", url = "http://www.mobile.mozilla.org"),
                ),
                selectedTabId = "d",
            ),
        )

        val uriFilter = "https://mozilla.org".toUri()
        val provider = SessionSuggestionProvider(
            store = store,
            selectTabUseCase = mock(),
            resultsUriFilter = {
                it.sameHostWithoutMobileSubdomainAs(uriFilter)
            },
            switchToTabDescription = "Switch to tab",
        )

        val suggestions = provider.onInputChanged("moz")

        assertEquals(5, suggestions.size)
        assertTrue(suggestions.map { it.title }.contains("https://mozilla.org/firefox"))
        assertTrue(suggestions.map { it.title }.contains("https://mozilla.org/focus"))
        assertTrue(suggestions.map { it.title }.contains("https://www.m.mozilla.org"))
        assertTrue(suggestions.map { it.title }.contains("http://www.mobile.mozilla.org"))
        assertTrue(suggestions.map { it.title }.contains("https://www.mozilla.org/vpn"))
    }

    @Test
    fun `GIVEN multiple tabs have the same url WHEN user inputs the same url THEN provider returns a single suggestion for the matching input`() = runTest {
        val store = BrowserStore()

        val url = "https://www.mozilla.org"
        val tab1 = createTab(url)
        val tab2 = createTab(url)
        val tab3 = createTab(url)

        val provider = SessionSuggestionProvider(store, mock(), switchToTabDescription = "Switch to tab")

        run {
            val suggestions = provider.onInputChanged("Mozilla")
            assertTrue(suggestions.isEmpty())
        }

        store.dispatch(TabListAction.AddTabAction(tab1)).join()
        store.dispatch(TabListAction.AddTabAction(tab2)).join()
        store.dispatch(TabListAction.AddTabAction(tab3)).join()

        run {
            val suggestions = provider.onInputChanged("Mozilla")
            assertEquals(1, suggestions.size)
            assertEquals(tab1.id, suggestions[0].id)
        }
    }
}
