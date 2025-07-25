/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.compose

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Card
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.minimumInteractiveComponentSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.onClick
import androidx.compose.ui.text.LinkAnnotation
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.TextLinkStyles
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import org.mozilla.fenix.R
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * The tag used for links in the text for annotated strings.
 */
private const val URL_TAG = "URL_TAG"

/**
 * Model containing link text, url and action.
 *
 * @property text Substring of the text passed to [LinkText] to be displayed as clickable link.
 * @property url Url the link should point to.
 * @property onClick Callback to be invoked when link is clicked.
 */
data class LinkTextState(
    val text: String,
    val url: String,
    val onClick: (String) -> Unit,
)

/**
 * A composable for displaying text that contains a clickable link text.
 *
 * @param text The complete text.
 * @param linkTextStates The clickable part of the text. The order of the states added in the list
 * should be the same as the links shown in the text.
 * @param style [TextStyle] applied to the text.
 * @param linkTextColor [Color] applied to the clickable part of the text.
 * @param linkTextDecoration [TextDecoration] applied to the clickable part of the text.
 * @param textAlign The alignment of the text within the lines of the paragraph. See [TextStyle.textAlign].
 * @param shouldApplyAccessibleSize determines whether a minimum interactive size should be applied
 * to improve accessibility touch targets.
 */
@Composable
fun LinkText(
    text: String,
    linkTextStates: List<LinkTextState>,
    style: TextStyle = FirefoxTheme.typography.body2.copy(
        textAlign = TextAlign.Center,
        color = FirefoxTheme.colors.textSecondary,
    ),
    linkTextColor: Color = FirefoxTheme.colors.textAccent,
    linkTextDecoration: TextDecoration = TextDecoration.None,
    textAlign: TextAlign? = null,
    shouldApplyAccessibleSize: Boolean = false,
) {
    val annotatedString = buildUrlAnnotatedString(
        text,
        linkTextStates,
        linkTextColor,
        linkTextDecoration,
    )

    val showDialog = remember { mutableStateOf(false) }
    val linksAvailable = stringResource(id = R.string.a11y_links_available)

    if (showDialog.value) {
        LinksDialog(linkTextStates) { showDialog.value = false }
    }

    val modifier = if (shouldApplyAccessibleSize) {
        Modifier.minimumInteractiveComponentSize()
    } else {
        Modifier
    }

    Text(
        text = annotatedString,
        style = style,
        modifier = modifier.clearAndSetSemantics {
            onClick {
                if (linkTextStates.size > 1) {
                    showDialog.value = true
                } else {
                    linkTextStates.firstOrNull()?.let {
                        it.onClick(it.url)
                    }
                }
                return@onClick true
            }
            contentDescription = "$annotatedString $linksAvailable"
        },
        textAlign = textAlign,
    )
}

@Composable
private fun LinksDialog(
    linkTextStates: List<LinkTextState>,
    onDismissRequest: () -> Unit,
) {
    Dialog(onDismissRequest = { onDismissRequest() }) {
        Card(
            modifier = Modifier
                .fillMaxWidth(),
            shape = RoundedCornerShape(8.dp),
        ) {
            Column(
                modifier = Modifier
                    .background(color = FirefoxTheme.colors.layer2)
                    .padding(all = 16.dp),
                horizontalAlignment = Alignment.CenterHorizontally,
            ) {
                Text(
                    text = stringResource(id = R.string.a11y_links_title),
                    color = FirefoxTheme.colors.textPrimary,
                    style = FirefoxTheme.typography.headline5,
                )

                linkTextStates.forEach { linkText ->
                    TextButton(
                        onClick = { linkText.onClick(linkText.url) },
                        modifier = Modifier
                            .align(Alignment.Start),
                    ) {
                        Text(
                            text = linkText.text,
                            color = FirefoxTheme.colors.textAccent,
                            textDecoration = TextDecoration.Underline,
                            style = FirefoxTheme.typography.button,
                        )
                    }
                }

                TextButton(
                    onClick = { onDismissRequest() },
                    modifier = Modifier
                        .align(Alignment.End),
                ) {
                    Text(
                        text = stringResource(id = R.string.standard_snackbar_error_dismiss),
                        color = FirefoxTheme.colors.textAccent,
                        style = FirefoxTheme.typography.button,
                    )
                }
            }
        }
    }
}

private fun buildUrlAnnotatedString(
    fullText: String,
    linkTextStates: List<LinkTextState>,
    color: Color,
    decoration: TextDecoration,
) = buildAnnotatedString {
    append(fullText)

    var previousWordEndIndex = 0

    linkTextStates.forEach { linkTextStates ->
        val link = LinkAnnotation.Clickable(
            tag = URL_TAG,
            styles = TextLinkStyles(
                SpanStyle(
                    color = color,
                    textDecoration = decoration,
                ),
            ),
            linkInteractionListener = {
                linkTextStates.onClick(linkTextStates.url)
            },
        )

        val startIndex = fullText.indexOf(linkTextStates.text, previousWordEndIndex)
        val endIndex = startIndex + linkTextStates.text.length

        addLink(link, startIndex, endIndex)

        previousWordEndIndex = endIndex
    }
}

@Preview
@Composable
private fun LinkTextEndPreview() {
    val state = LinkTextState(
        text = "click here",
        url = "www.mozilla.com",
        onClick = {},
    )
    FirefoxTheme {
        Box(modifier = Modifier.background(color = FirefoxTheme.colors.layer1)) {
            LinkText(text = "This is normal text, click here", linkTextStates = listOf(state))
        }
    }
}

@Preview
@Composable
private fun LinkTextMiddlePreview() {
    val state = LinkTextState(
        text = "clickable text",
        url = "www.mozilla.com",
        onClick = {},
    )
    FirefoxTheme {
        Box(modifier = Modifier.background(color = FirefoxTheme.colors.layer1)) {
            LinkText(text = "This is clickable text, followed by normal text", linkTextStates = listOf(state))
        }
    }
}

@Preview
@Composable
private fun LinkTextStyledPreview() {
    val state = LinkTextState(
        text = "clickable text",
        url = "www.mozilla.com",
        onClick = {},
    )
    FirefoxTheme {
        Box(modifier = Modifier.background(color = FirefoxTheme.colors.layer1)) {
            LinkText(
                text = "This is clickable text, in a different style",
                linkTextStates = listOf(state),
                style = FirefoxTheme.typography.headline5,
            )
        }
    }
}

@Preview
@Composable
private fun LinkTextClickStyledPreview() {
    val state = LinkTextState(
        text = "clickable text",
        url = "www.mozilla.com",
        onClick = {},
    )
    FirefoxTheme {
        Box(modifier = Modifier.background(color = FirefoxTheme.colors.layer1)) {
            LinkText(
                text = "This is clickable text, with underlined text",
                linkTextStates = listOf(state),
                style = FirefoxTheme.typography.headline5,
                linkTextColor = FirefoxTheme.colors.textOnColorSecondary,
                linkTextDecoration = TextDecoration.Underline,
            )
        }
    }
}

@Preview
@Composable
private fun MultipleLinksPreview() {
    val state1 = LinkTextState(
        text = "clickable text",
        url = "www.mozilla.com",
        onClick = {},
    )

    val state2 = LinkTextState(
        text = "another clickable text",
        url = "www.mozilla.com",
        onClick = {},
    )
    FirefoxTheme {
        Box(modifier = Modifier.background(color = FirefoxTheme.colors.layer1)) {
            LinkText(
                text = "This is clickable text, followed by normal text, followed by another clickable text",
                linkTextStates = listOf(state1, state2),
            )
        }
    }
}

@Preview
@Composable
private fun LinksDialogPreview() {
    val state1 = LinkTextState(
        text = "clickable text",
        url = "www.mozilla.com",
        onClick = {},
    )

    val state2 = LinkTextState(
        text = "another clickable text",
        url = "www.mozilla.com",
        onClick = {},
    )

    val linkTextStateList = listOf(state1, state2)
    FirefoxTheme {
        LinksDialog(
            linkTextStates = linkTextStateList,
            onDismissRequest = {},
        )
    }
}
