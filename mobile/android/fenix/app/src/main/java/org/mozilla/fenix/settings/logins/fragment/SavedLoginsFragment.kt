/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.logins.fragment

import android.content.Intent
import android.os.Bundle
import android.view.LayoutInflater
import android.view.Menu
import android.view.MenuInflater
import android.view.MenuItem
import android.view.View
import android.view.ViewGroup
import android.view.inputmethod.EditorInfo
import android.widget.FrameLayout
import android.widget.ImageView
import androidx.activity.result.ActivityResultLauncher
import androidx.appcompat.app.AppCompatActivity
import androidx.appcompat.widget.SearchView
import androidx.appcompat.widget.Toolbar
import androidx.compose.ui.platform.ComposeView
import androidx.compose.ui.platform.ViewCompositionStrategy
import androidx.constraintlayout.widget.ConstraintLayout
import androidx.core.content.getSystemService
import androidx.core.os.bundleOf
import androidx.core.view.MenuProvider
import androidx.core.view.isVisible
import androidx.fragment.app.setFragmentResult
import androidx.fragment.app.setFragmentResultListener
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.lifecycleScope
import androidx.navigation.NavHostController
import androidx.navigation.fragment.findNavController
import mozilla.components.concept.engine.EngineSession
import mozilla.components.concept.menu.MenuController
import mozilla.components.concept.menu.Orientation
import mozilla.components.lib.state.ext.consumeFrom
import org.mozilla.fenix.BrowserDirection
import org.mozilla.fenix.HomeActivity
import org.mozilla.fenix.R
import org.mozilla.fenix.SecureFragment
import org.mozilla.fenix.biometricauthentication.AuthenticationStatus
import org.mozilla.fenix.biometricauthentication.BiometricAuthenticationManager
import org.mozilla.fenix.components.StoreProvider
import org.mozilla.fenix.databinding.FragmentSavedLoginsBinding
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.hideToolbar
import org.mozilla.fenix.ext.registerForActivityResult
import org.mozilla.fenix.ext.settings
import org.mozilla.fenix.ext.showToolbar
import org.mozilla.fenix.lifecycle.LifecycleHolder
import org.mozilla.fenix.settings.biometric.DefaultBiometricUtils
import org.mozilla.fenix.settings.logins.LoginsAction
import org.mozilla.fenix.settings.logins.LoginsFragmentStore
import org.mozilla.fenix.settings.logins.LoginsListState
import org.mozilla.fenix.settings.logins.SavedLoginsSortingStrategyMenu
import org.mozilla.fenix.settings.logins.SortingStrategy
import org.mozilla.fenix.settings.logins.controller.LoginsListController
import org.mozilla.fenix.settings.logins.controller.SavedLoginsStorageController
import org.mozilla.fenix.settings.logins.createInitialLoginsListState
import org.mozilla.fenix.settings.logins.interactor.SavedLoginsInteractor
import org.mozilla.fenix.settings.logins.ui.DefaultSavedLoginsStorage
import org.mozilla.fenix.settings.logins.ui.LoginsMiddleware
import org.mozilla.fenix.settings.logins.ui.LoginsSortOrder
import org.mozilla.fenix.settings.logins.ui.LoginsState
import org.mozilla.fenix.settings.logins.ui.LoginsStore
import org.mozilla.fenix.settings.logins.ui.SavedLoginsScreen
import org.mozilla.fenix.settings.logins.view.SavedLoginsListView
import org.mozilla.fenix.theme.FirefoxTheme

@SuppressWarnings("TooManyFunctions")
class SavedLoginsFragment : SecureFragment(), MenuProvider {
    private lateinit var savedLoginsStore: LoginsFragmentStore
    private lateinit var savedLoginsListView: SavedLoginsListView
    private lateinit var savedLoginsInteractor: SavedLoginsInteractor
    private lateinit var dropDownMenuAnchorView: View
    private lateinit var sortingStrategyMenu: SavedLoginsSortingStrategyMenu
    private lateinit var toolbarChildContainer: FrameLayout
    private lateinit var sortLoginsMenuRoot: ConstraintLayout
    private lateinit var loginsListController: LoginsListController
    private lateinit var savedLoginsStorageController: SavedLoginsStorageController

    private lateinit var loginState: LoginsListState
    private var removedLoginGuid: String? = null
    private var deletedGuid = mutableSetOf<String>()
    private var searchQuery: LoginsListState? = null

    private var _binding: FragmentSavedLoginsBinding? = null
    private val binding get() = _binding!!
    private lateinit var startForResult: ActivityResultLauncher<Intent>

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        if (requireContext().settings().enableComposeLogins) { return }

        startForResult = registerForActivityResult {
            BiometricAuthenticationManager.biometricAuthenticationNeededInfo.shouldShowAuthenticationPrompt =
                false
            BiometricAuthenticationManager.biometricAuthenticationNeededInfo.authenticationStatus =
                AuthenticationStatus.AUTHENTICATED
            setSecureContentVisibility(true)
        }
    }

    override fun onResume() {
        super.onResume()

        if (requireContext().settings().enableComposeLogins) {
            hideToolbar()
            return
        }

        if (BiometricAuthenticationManager.biometricAuthenticationNeededInfo.shouldShowAuthenticationPrompt) {
            BiometricAuthenticationManager.biometricAuthenticationNeededInfo.shouldShowAuthenticationPrompt =
                false
            BiometricAuthenticationManager.biometricAuthenticationNeededInfo.authenticationStatus =
                AuthenticationStatus.AUTHENTICATION_IN_PROGRESS
            setSecureContentVisibility(false)

            DefaultBiometricUtils.bindBiometricsCredentialsPromptOrShowWarning(
                view = requireView(),
                onShowPinVerification = { intent -> startForResult.launch(intent) },
                onAuthSuccess = {
                    BiometricAuthenticationManager.biometricAuthenticationNeededInfo.authenticationStatus =
                        AuthenticationStatus.AUTHENTICATED
                    setSecureContentVisibility(true)
                },
                onAuthFailure = {
                    BiometricAuthenticationManager.biometricAuthenticationNeededInfo.authenticationStatus =
                        AuthenticationStatus.NOT_AUTHENTICATED
                    setSecureContentVisibility(false)
                },
            )
        } else {
            setSecureContentVisibility(
                BiometricAuthenticationManager.biometricAuthenticationNeededInfo.authenticationStatus ==
                    AuthenticationStatus.AUTHENTICATED,
            )
        }
        initToolbar()
    }

    @Suppress("LongMethod")
    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ): View? {
        if (requireContext().settings().enableComposeLogins) {
            return ComposeView(requireContext()).apply {
                setViewCompositionStrategy(ViewCompositionStrategy.DisposeOnViewTreeLifecycleDestroyed)
                val buildStore = { navController: NavHostController ->
                    val store = StoreProvider.get(this@SavedLoginsFragment) {
                        val lifecycleHolder = LifecycleHolder(
                            context = requireContext(),
                            navController = this@SavedLoginsFragment.findNavController(),
                            composeNavController = navController,
                            homeActivity = (requireActivity() as HomeActivity),
                        )

                        LoginsStore(
                            initialState = LoginsState().copy(
                                sortOrder = LoginsSortOrder.fromString(
                                    value = requireContext().settings().loginsListSortOrder,
                                    default = LoginsSortOrder.Alphabetical,
                                ),
                            ),
                            middleware = listOf(
                                LoginsMiddleware(
                                    loginsStorage = requireContext().components.core.passwordsStorage,
                                    getNavController = { lifecycleHolder.composeNavController },
                                    exitLogins = { lifecycleHolder.navController.popBackStack() },
                                    persistLoginsSortOrder = {
                                        DefaultSavedLoginsStorage(
                                            lifecycleHolder.context.settings(),
                                        ).savedLoginsSortOrder = it
                                    },
                                    openTab = { url, openInNewTab ->
                                        lifecycleHolder.homeActivity.openToBrowserAndLoad(
                                            searchTermOrURL = url,
                                            newTab = openInNewTab,
                                            from = BrowserDirection.FromSavedLoginsFragment,
                                            flags = EngineSession.LoadUrlFlags.select(
                                                EngineSession.LoadUrlFlags.ALLOW_JAVASCRIPT_URL,
                                            ),
                                        )
                                    },
                                    clipboardManager = requireActivity().getSystemService(),
                                ),
                            ),
                            lifecycleHolder = lifecycleHolder,
                        )
                    }

                    store.lifecycleHolder?.apply {
                        this.navController = this@SavedLoginsFragment.findNavController()
                        this.composeNavController = navController
                        this.homeActivity = (requireActivity() as HomeActivity)
                        this.context = requireContext()
                    }

                    store
                }
                setContent {
                    FirefoxTheme {
                        SavedLoginsScreen(buildStore = buildStore)
                    }
                }
            }
        }
        val view = inflater.inflate(R.layout.fragment_saved_logins, container, false)

        _binding = FragmentSavedLoginsBinding.bind(view)

        savedLoginsStore =
            StoreProvider.get(findNavController().getBackStackEntry(R.id.savedLogins)) {
                LoginsFragmentStore(
                    createInitialLoginsListState(requireContext().settings()),
                )
            }

        loginsListController =
            LoginsListController(
                loginsFragmentStore = savedLoginsStore,
                navController = findNavController(),
                browserNavigator = ::openToBrowserAndLoad,
                addLoginCallback = {
                    searchQuery = null
                },
                settings = requireContext().settings(),
            )
        savedLoginsStorageController =
            SavedLoginsStorageController(
                passwordsStorage = requireContext().components.core.passwordsStorage,
                lifecycleScope = viewLifecycleOwner.lifecycleScope,
                navController = findNavController(),
                loginsFragmentStore = savedLoginsStore,
                clipboardHandler = requireContext().components.clipboardHandler,
            )

        savedLoginsInteractor =
            SavedLoginsInteractor(
                loginsListController,
                savedLoginsStorageController,
            )

        savedLoginsListView = SavedLoginsListView(
            binding.savedLoginsLayout,
            savedLoginsInteractor,
        )
        savedLoginsInteractor.loadAndMapLogins()
        return view
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        if (requireContext().settings().enableComposeLogins) {
            return
        }

        setFragmentResultListener(LoginDetailFragment.LOGIN_REQUEST_KEY) { _, bundle ->
            removedLoginGuid = bundle.getString(LoginDetailFragment.LOGIN_BUNDLE_ARGS)
            deletedGuid.add(removedLoginGuid.toString())
        }
        consumeFrom(savedLoginsStore) { loginsListState ->
            if (!this::sortingStrategyMenu.isInitialized) {
                sortingStrategyMenu =
                    SavedLoginsSortingStrategyMenu(requireContext(), savedLoginsInteractor)
            }
            sortingStrategyMenu.updateMenu(savedLoginsStore.state.highlightedItem)
            loginState = loginsListState
            val currentList = loginState.filteredItems.toMutableList()

            if (removedLoginGuid != null) {
                val newList = currentList.filter { !deletedGuid.contains(it.guid) }

                loginState = loginState.copy(
                    loginList = newList,
                    filteredItems = newList,
                )
            }
            savedLoginsListView.update(loginState)
        }
    }

    override fun onCreateMenu(menu: Menu, inflater: MenuInflater) {
        inflater.inflate(R.menu.login_list, menu)

        val searchItem = menu.findItem(R.id.search)
        val searchView: SearchView = searchItem.actionView as SearchView
        searchView.imeOptions = EditorInfo.IME_ACTION_DONE
        searchView.queryHint = getString(R.string.preferences_passwords_saved_logins_search_2)
        searchView.maxWidth = Int.MAX_VALUE

        if (searchQuery?.searchedForText?.isNotEmpty() == true) {
            searchItem.expandActionView()
            searchView.setQuery(searchQuery?.searchedForText, true)
            searchView.clearFocus()
            filterSavedLogins(searchQuery?.searchedForText)
        }

        searchView.setOnQueryTextListener(
            object : SearchView.OnQueryTextListener {
                override fun onQueryTextSubmit(query: String?): Boolean {
                    return false
                }

                override fun onQueryTextChange(newText: String?): Boolean {
                    if (newText?.isNotEmpty() == true) {
                        searchQuery = savedLoginsStore.state.copy(
                            searchedForText = newText,
                        )
                    }
                    filterSavedLogins(newText)
                    return false
                }
            },
        )

        val closeButton: ImageView = searchView.findViewById(R.id.search_close_btn) as ImageView
        closeButton.setOnClickListener {
            searchView.setQuery("", false)
            searchQuery = savedLoginsStore.state.copy(
                searchedForText = null,
            )
        }
    }

    private fun filterSavedLogins(query: String?) {
        savedLoginsStore.dispatch(
            LoginsAction.FilterLogins(
                query,
            ),
        )
    }

    override fun onMenuItemSelected(menuItem: MenuItem): Boolean = false

    /**
     * If we pause this fragment, we want to pop users back to reauth
     */
    override fun onPause() {
        if (requireContext().settings().enableComposeLogins) {
            super.onPause()
            return
        }

        toolbarChildContainer.removeAllViews()
        toolbarChildContainer.visibility = View.GONE
        (activity as HomeActivity).getSupportActionBarAndInflateIfNecessary()
            .setDisplayShowTitleEnabled(true)
        sortingStrategyMenu.menuController.dismiss()
        sortLoginsMenuRoot.setOnClickListener(null)

        setFragmentResult(
            LoginDetailFragment.HAS_QUERY_KEY,
            bundleOf(LoginDetailFragment.HAS_QUERY_BUNDLE to searchQuery?.searchedForText),
        )
        super.onPause()
    }

    private fun openToBrowserAndLoad(
        searchTermOrURL: String,
        newTab: Boolean,
        from: BrowserDirection,
    ) = (activity as HomeActivity).openToBrowserAndLoad(searchTermOrURL, newTab, from)

    private fun initToolbar() {
        requireActivity().addMenuProvider(this, viewLifecycleOwner, Lifecycle.State.RESUMED)
        showToolbar(getString(R.string.preferences_passwords_saved_logins_2))
        (activity as HomeActivity).getSupportActionBarAndInflateIfNecessary()
            .setDisplayShowTitleEnabled(false)
        toolbarChildContainer = initChildContainerFromToolbar()
        sortLoginsMenuRoot = inflateSortLoginsMenuRoot()
        dropDownMenuAnchorView = sortLoginsMenuRoot.findViewById(R.id.drop_down_menu_anchor_view)
        when (requireContext().settings().savedLoginsSortingStrategy) {
            is SortingStrategy.Alphabetically -> setupMenu(
                SavedLoginsSortingStrategyMenu.Item.AlphabeticallySort,
            )

            is SortingStrategy.LastUsed -> setupMenu(
                SavedLoginsSortingStrategyMenu.Item.LastUsedSort,
            )
        }
    }

    private fun initChildContainerFromToolbar(): FrameLayout {
        val activity = activity as? AppCompatActivity
        val toolbar = (activity as HomeActivity).findViewById<Toolbar>(R.id.navigationToolbar)

        return (toolbar.findViewById(R.id.toolbar_child_container) as FrameLayout).apply {
            visibility = View.VISIBLE
        }
    }

    private fun inflateSortLoginsMenuRoot(): ConstraintLayout {
        return LayoutInflater.from(context)
            .inflate(R.layout.saved_logins_sort_items_toolbar_child, toolbarChildContainer, true)
            .findViewById(R.id.sort_logins_menu_root)
    }

    private fun attachMenu() {
        sortingStrategyMenu.menuController.register(
            object : MenuController.Observer {
                override fun onDismiss() {
                    // Deactivate button on dismiss
                    sortLoginsMenuRoot.isActivated = false
                }
            },
            view = sortLoginsMenuRoot,
        )

        sortLoginsMenuRoot.setOnClickListener {
            // Activate button on show
            sortLoginsMenuRoot.isActivated = true
            sortingStrategyMenu.menuController.show(
                anchor = dropDownMenuAnchorView,
                orientation = Orientation.DOWN,
            )
        }
    }

    private fun setupMenu(itemToHighlight: SavedLoginsSortingStrategyMenu.Item) {
        sortingStrategyMenu =
            SavedLoginsSortingStrategyMenu(requireContext(), savedLoginsInteractor)
        sortingStrategyMenu.updateMenu(itemToHighlight)

        attachMenu()
    }

    private fun setSecureContentVisibility(isVisible: Boolean) {
        binding.savedLoginsLayout.isVisible = isVisible
    }
}
