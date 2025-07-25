/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

@file:Suppress("TooManyFunctions")

package org.mozilla.fenix.tabstray.ui.inactivetabs

import androidx.compose.animation.animateContentSize
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.painter.BitmapPainter
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import mozilla.components.browser.state.state.ContentState
import mozilla.components.browser.state.state.TabSessionState
import mozilla.components.compose.base.button.TextButton
import mozilla.components.compose.cfr.CFRPopup
import mozilla.components.compose.cfr.CFRPopupLayout
import mozilla.components.compose.cfr.CFRPopupProperties
import org.mozilla.fenix.R
import org.mozilla.fenix.compose.list.ExpandableListHeader
import org.mozilla.fenix.compose.list.FaviconListItem
import org.mozilla.fenix.ext.toShortUrl
import org.mozilla.fenix.tabstray.ext.toDisplayTitle
import org.mozilla.fenix.theme.FirefoxTheme

private val ROUNDED_CORNER_SHAPE = RoundedCornerShape(8.dp)

/**
 * Top-level list for displaying an expandable section of Inactive Tabs.
 *
 * @param inactiveTabs List of [TabSessionState] to display.
 * @param expanded Whether to show the inactive tabs section expanded or collapsed.
 * @param showAutoCloseDialog Whether to show the auto close inactive tabs dialog.
 * @param showCFR Whether to show the CFR.
 * @param onHeaderClick Called when the user clicks on the inactive tabs section header.
 * @param onDeleteAllButtonClick Called when the user clicks on the delete all inactive tabs button.
 * @param onAutoCloseDismissClick Called when the user clicks on the auto close dialog's dismiss button.
 * @param onEnableAutoCloseClick Called when the user clicks on the auto close dialog's enable button.
 * @param onTabClick Called when the user clicks on a specific inactive tab.
 * @param onTabCloseClick Called when the user clicks on a specific inactive tab's close button.
 * @param onCFRShown Invoked when the CFR is displayed.
 * @param onCFRClick Invoked when the CFR is clicked.
 * @param onCFRDismiss Invoked when the CFR is dismissed.
 */
@Composable
@Suppress("LongParameterList")
fun InactiveTabsList(
    inactiveTabs: List<TabSessionState>,
    expanded: Boolean,
    showAutoCloseDialog: Boolean,
    showCFR: Boolean,
    onHeaderClick: (Boolean) -> Unit,
    onDeleteAllButtonClick: () -> Unit,
    onAutoCloseDismissClick: () -> Unit,
    onEnableAutoCloseClick: () -> Unit,
    onTabClick: (TabSessionState) -> Unit,
    onTabCloseClick: (TabSessionState) -> Unit,
    onCFRShown: () -> Unit,
    onCFRClick: () -> Unit,
    onCFRDismiss: () -> Unit,
) {
    Card(
        modifier = Modifier.padding(horizontal = 16.dp, vertical = 12.dp),
        colors = CardDefaults.cardColors(containerColor = FirefoxTheme.colors.layer2),
        shape = ROUNDED_CORNER_SHAPE,
        border = BorderStroke(
            width = 1.dp,
            color = FirefoxTheme.colors.borderPrimary,
        ),
    ) {
        Column(
            modifier = Modifier
                .animateContentSize()
                .fillMaxWidth(),
        ) {
            InactiveTabsHeader(
                expanded = expanded,
                showCFR = showCFR,
                onCFRShown = onCFRShown,
                onCFRClick = onCFRClick,
                onCFRDismiss = onCFRDismiss,
                onClick = { onHeaderClick(!expanded) },
                onDeleteAllClick = onDeleteAllButtonClick,
            )

            if (expanded) {
                if (showAutoCloseDialog) {
                    InactiveTabsAutoClosePrompt(
                        onDismissClick = onAutoCloseDismissClick,
                        onEnableAutoCloseClick = onEnableAutoCloseClick,
                    )
                }

                Column {
                    inactiveTabs.forEach { tab ->
                        val tabUrl = tab.content.url.toShortUrl()
                        val faviconPainter = tab.content.icon?.run {
                            prepareToDraw()
                            BitmapPainter(asImageBitmap())
                        }

                        FaviconListItem(
                            label = tab.toDisplayTitle(),
                            url = tabUrl,
                            description = tabUrl,
                            faviconPainter = faviconPainter,
                            onClick = { onTabClick(tab) },
                            iconPainter = painterResource(R.drawable.mozac_ic_cross_24),
                            iconDescription = stringResource(R.string.content_description_close_button),
                            onIconClick = { onTabCloseClick(tab) },
                        )
                    }
                }

                Spacer(modifier = Modifier.height(8.dp))
            }
        }
    }
}

/**
 * Collapsible header for the Inactive Tabs section with a CFR.
 *
 * @param expanded Whether the section is expanded.
 * @param showCFR Whether to show the CFR.
 * @param onClick Called when the user clicks on the header.
 * @param onDeleteAllClick Called when the user clicks on the delete all button.
 * @param onCFRShown Invoked when the CFR is displayed.
 * @param onCFRClick Invoked when the CFR is clicked.
 * @param onCFRDismiss Invoked when the CFR is dismissed.
 */
@Composable
private fun InactiveTabsHeader(
    expanded: Boolean,
    showCFR: Boolean,
    onClick: () -> Unit,
    onDeleteAllClick: () -> Unit,
    onCFRShown: () -> Unit,
    onCFRClick: () -> Unit,
    onCFRDismiss: () -> Unit,
) {
    CFRPopupLayout(
        showCFR = showCFR,
        properties = CFRPopupProperties(
            popupBodyColors = listOf(
                FirefoxTheme.colors.layerGradientEnd.toArgb(),
                FirefoxTheme.colors.layerGradientStart.toArgb(),
            ),
            dismissButtonColor = FirefoxTheme.colors.iconOnColor.toArgb(),
            indicatorDirection = CFRPopup.IndicatorDirection.UP,
            popupVerticalOffset = (-12).dp,
        ),
        onCFRShown = onCFRShown,
        onDismiss = { onCFRDismiss() },
        text = {
            FirefoxTheme {
                Text(
                    text = stringResource(R.string.tab_tray_inactive_onboarding_message),
                    color = FirefoxTheme.colors.textOnColorPrimary,
                    style = FirefoxTheme.typography.body2,
                )
            }
        },
        action = { dismissCFR ->
            FirefoxTheme {
                Text(
                    text = stringResource(R.string.tab_tray_inactive_onboarding_button_text),
                    color = FirefoxTheme.colors.textOnColorPrimary,
                    modifier = Modifier.clickable {
                        dismissCFR()
                        onCFRClick()
                    },
                    style = FirefoxTheme.typography.body2.copy(
                        textDecoration = TextDecoration.Underline,
                    ),
                )
            }
        },
    ) {
        ExpandableListHeader(
            headerText = stringResource(R.string.inactive_tabs_title),
            headerTextStyle = FirefoxTheme.typography.headline7,
            expanded = expanded,
            expandActionContentDescription = stringResource(R.string.inactive_tabs_expand_content_description),
            collapseActionContentDescription = stringResource(R.string.inactive_tabs_collapse_content_description),
            onClick = onClick,
        ) {
            IconButton(
                onClick = onDeleteAllClick,
                modifier = Modifier.padding(horizontal = 4.dp),
            ) {
                Icon(
                    painter = painterResource(R.drawable.ic_delete),
                    contentDescription = stringResource(R.string.inactive_tabs_delete_all),
                    tint = FirefoxTheme.colors.iconPrimary,
                )
            }
        }
    }
}

/**
 * Inactive Tabs auto close dialog.
 *
 * @param onDismissClick Called when the user clicks on the auto close dialog's dismiss button.
 * @param onEnableAutoCloseClick Called when the user clicks on the auto close dialog's enable button.
 */
@Composable
private fun InactiveTabsAutoClosePrompt(
    onDismissClick: () -> Unit,
    onEnableAutoCloseClick: () -> Unit,
) {
    Card(
        modifier = Modifier.padding(horizontal = 16.dp, vertical = 12.dp),
        shape = ROUNDED_CORNER_SHAPE,
        colors = CardDefaults.cardColors(containerColor = FirefoxTheme.colors.layer2),
        border = BorderStroke(
            width = 1.dp,
            color = FirefoxTheme.colors.borderPrimary,
        ),
    ) {
        Column(
            modifier = Modifier.padding(horizontal = 12.dp),
            horizontalAlignment = Alignment.End,
        ) {
            Spacer(modifier = Modifier.height(12.dp))

            Row(
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text(
                    text = stringResource(R.string.tab_tray_inactive_auto_close_title),
                    color = FirefoxTheme.colors.textPrimary,
                    modifier = Modifier.weight(1f),
                    style = FirefoxTheme.typography.headline8,
                )

                IconButton(
                    onClick = onDismissClick,
                    modifier = Modifier.size(20.dp),
                ) {
                    Icon(
                        painter = painterResource(R.drawable.mozac_ic_cross_20),
                        contentDescription =
                        stringResource(R.string.tab_tray_inactive_auto_close_button_content_description),
                        tint = FirefoxTheme.colors.iconPrimary,
                    )
                }
            }

            Text(
                text = stringResource(
                    R.string.tab_tray_inactive_auto_close_body_2,
                    stringResource(R.string.app_name),
                ),
                color = FirefoxTheme.colors.textSecondary,
                modifier = Modifier.fillMaxWidth(),
                fontSize = 14.sp,
            )

            TextButton(
                text = stringResource(R.string.tab_tray_inactive_turn_on_auto_close_button_2),
                onClick = onEnableAutoCloseClick,
            )
        }
    }
}

@Composable
@PreviewLightDark
private fun InactiveTabsAutoClosePromptPreview() {
    FirefoxTheme {
        Box(Modifier.background(FirefoxTheme.colors.layer1)) {
            InactiveTabsAutoClosePrompt(
                onDismissClick = {},
                onEnableAutoCloseClick = {},
            )
        }
    }
}

@Composable
@PreviewLightDark
private fun InactiveTabsListPreview() {
    var expanded by remember { mutableStateOf(true) }
    var showAutoClosePrompt by remember { mutableStateOf(true) }

    FirefoxTheme {
        Box(Modifier.background(FirefoxTheme.colors.layer1)) {
            InactiveTabsList(
                inactiveTabs = generateFakeInactiveTabsList(),
                expanded = expanded,
                showAutoCloseDialog = showAutoClosePrompt,
                showCFR = false,
                onHeaderClick = { expanded = !expanded },
                onDeleteAllButtonClick = {},
                onAutoCloseDismissClick = { showAutoClosePrompt = !showAutoClosePrompt },
                onEnableAutoCloseClick = { showAutoClosePrompt = !showAutoClosePrompt },
                onTabClick = {},
                onTabCloseClick = {},
                onCFRShown = {},
                onCFRClick = {},
                onCFRDismiss = {},
            )
        }
    }
}

private fun generateFakeInactiveTabsList(): List<TabSessionState> =
    listOf(
        TabSessionState(
            id = "tabId",
            content = ContentState(
                url = "www.mozilla.com",
            ),
        ),
        TabSessionState(
            id = "tabId",
            content = ContentState(
                url = "www.google.com",
            ),
        ),
    )
