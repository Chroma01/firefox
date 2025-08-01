# Trigger Listeners

A set of action listeners that can be used to trigger CFR messages.

## Usage

[As part of the CFR definition](https://searchfox.org/mozilla-central/rev/2bfe3415fb3a2fba9b1c694bc0b376365e086927/browser/components/newtab/lib/CFRMessageProvider.jsm#194) the message can register at most one trigger used to decide when the message is shown.

Most triggers (unless otherwise specified) take the same arguments of `hosts` and/or `patterns`
used to target the message to specific websites.

```javascript
// Optional set of hosts to filter out triggers only to certain websites
let params: string[];
// Optional set of [match patterns](https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/Match_patterns) to filter out triggers only to certain websites
let patterns: string[];
```

```javascript
{
  ...
  // Show the message when opening mozilla.org
  "trigger": { "id": "openURL", "params": ["mozilla.org", "www.mozilla.org"] }
  ...
}
```

```javascript
{
  ...
  // Show the message when opening any HTTP, HTTPS URL.
  trigger: { id: "openURL", patterns: ["*://*/*"] }
  ...
}
```

## Available trigger actions

- [`openArticleURL`](#openarticleurl)
- [`openBookmarkedURL`](#openbookmarkedurl)
- [`frequentVisits`](#frequentvisits)
- [`openURL`](#openurl)
- [`newSavedLogin`](#newsavedlogin)
- [`formAutofill`](#formautofill)
- [`contentBlocking`](#contentblocking)
- [`defaultBrowserCheck`](#defaultbrowsercheck)
- [`deeplinkedToWindowsSettingsUI`](#deeplinkedtowindowssettingsui)
- [`captivePortalLogin`](#captiveportallogin)
- [`preferenceObserver`](#preferenceobserver)
- [`featureCalloutCheck`](#featurecalloutcheck)
- [`pdfJsFeatureCalloutCheck`](#pdfjsfeaturecalloutcheck)
- [`newtabFeatureCalloutCheck`](#newtabfeaturecalloutcheck)
- [`nthTabClosed`](#nthtabclosed)
- [`nthTabOpened`](#nthtabopened)
- [`tabGroupCreated`](#tabgroupcreated)
- [`tabGroupSaved`](#tabgroupsaved)
- [`tabGroupCollapsed`](#tabgroupcollapsed)
- [`activityAfterIdle`](#activityafteridle)
- [`cookieBannerDetected`](#cookiebannerdetected)
- [`cookieBannerHandled`](#cookiebannerhandled)
- [`messagesLoaded`](#messagesloaded)
- [`pageActionInUrlbar`](#pageactioninurlbar)
- [`onSearch`](#onsearch)
- [`sidebarToolOpened`](#sidebartoolopened)
- [`elementClicked`](#elementclicked)

### `openArticleURL`

Happens when the user loads a Reader Mode compatible webpage.

### `openBookmarkedURL`

Happens when the user bookmarks or navigates to a bookmarked URL.

Does not filter by host or patterns.

### `frequentVisits`

Happens every time a user navigates (or switches tab) to any of the `hosts`
or `patterns` arguments provided. This trigger adds an item to the targeting
context called `recentVisits`. It is a sorted array of timestamps for recent
visits to the hosts provided in the `hosts` property.
This can be used inside of the targeting expression:

```javascript
// Has at least 3 visits in the past hour
recentVisits[.timestamp > (currentDate|date - 3600 * 1000 * 1)]|length >= 3

```

```typescript
interface visit {
  host: string,
  timestamp: UnixTimestamp
};
// Host and timestamp for every visit to "Host"
let recentVisits: visit[];
```

### `openURL`

Happens every time the user loads a new URL that matches the provided `hosts` or `patterns`.
During a browsing session it keeps track of visits to unique urls that can be used inside targeting expression.

```javascript
// True on the third visit for the URL which the trigger matched on
visitsCount >= 3
```

### `newSavedLogin`

Happens every time the user saves or updates a login via the login capture doorhanger.
Provides a `type` to differentiate between the two events that can be used in targeting.

Does not filter by host or patterns.

```typescript
let type = "update" | "save";
```

### `formAutofill`

Happens when the user saves, updates, or uses a credit card or address for form
autofill. To reduce the trigger's disruptiveness, it does not fire when the user
is manually editing these items in the manager in about:preferences. For the
same reason, the trigger only fires after a 10-second delay. The trigger context
includes an `event` and `type` that can be used in targeting. Possible events
include `add`, `update`, and `use`. Possible types are `card` and `address`.
This trigger is especially intended to be used in tandem with the
`creditCardsSaved` and `addressesSaved` [targeting attributes](/browser/components/asrouter/docs/targeting-attributes.md).

```js
{
  trigger: { id: "formAutofill" },
  targeting: "type == 'card' && event in ['add', 'update']"
}
```

### `contentBlocking`

Happens at the and of a document load and for every subsequent content blocked event, or when the tracking DB service hits a milestone.

Provides a context of the number of pages loaded in the current browsing session that can be used in targeting.

Does not filter by host or patterns.

The event it reports back is one of two things:
 * A combination of OR-ed [nsIWebProgressListener](https://searchfox.org/mozilla-central/source/uriloader/base/nsIWebProgressListener.idl) `STATE_BLOCKED_*` flags
 * A string constant, such as [`"ContentBlockingMilestone"`](https://searchfox.org/mozilla-central/rev/8a2d8d26e25ef70c98c6036612aad534b76b9815/toolkit/components/antitracking/TrackingDBService.jsm#327-334)


### `defaultBrowserCheck`

Happens at startup, when opening a newtab and when navigating to about:home.
At startup, it reports the `source` as `startup`, and it provides a context
attribute `willShowDefaultPrompt` that can be used in targeting to avoid showing
a message when the built-in default browser prompt is going to be displayed.
This is important to avoid the negative UX of showing two promts back-to-back,
especially if both prompts offer similar affordances.
On the newtab/homepage, it reports the `source` as `newtab`.

```ts
let source = "startup" | "newtab";
let willShowDefaultPrompt = boolean | undefined;
```

#### Examples

* Only trigger on startup, not on newtab/homepage
* Don't show if the built-in prompt is going to be shown

```js
{
  trigger: { id: "defaultBrowserCheck" },
  targeting: "source == 'startup' && !willShowDefaultPrompt"
}
```

### `deeplinkedToWindowsSettingsUI`

Triggers when the user has indicated they want to set Firefox as the default web
browser and interaction with Windows Settings is necessary to finish setting
Firefox as default.

### `captivePortalLogin`

Happens when the user successfully goes through a captive portal authentication flow.

### `preferenceObserver`

Watch for changes on any number of preferences. Runs when a pref is added, removed or modified.

```js
// Register a message with the following trigger
{
  id: "preferenceObserver",
  params: ["pref name"]
}
```

### `featureCalloutCheck`

Used to display Feature Callouts in Firefox View. Can only be used for Feature Callouts.

### `pdfJsFeatureCalloutCheck`

Used to display Feature Callouts on PDF.js pages. Can only be used for Feature Callouts.

### `newtabFeatureCalloutCheck`

Used to display Feature Callouts on about:newtab. Can only be used for Feature Callouts.

### `nthTabClosed`

Happens when the user closes n or more tabs in a session

```js
// Register a message with the following trigger and
// include the tabsClosedCount context variable in the targeting.
// Here, the message triggers after two or more tabs are closed.
{
  trigger: { id: "nthTabClosed" },
  targeting: "tabsClosedCount >= 2"
}
```
```js
// The trigger also tracks the number of tabs currently open,
// and the currentTabsOpen context variable can be used in targeting
// to ensure a minimum number of tabs are open.
// Here, the message will trigger on the next tab closed
// after 4 tabs are opened (and remain open).
{
  trigger: { id: "nthTabClosed" },
  targeting: "currentTabsOpen >= 4"
}
```

### `nthTabOpened`

Happens when the user opens n or more tabs in a session

```js
// Register a message with the following trigger and
// include the tabsOpenedCount context variable in the targeting.
// Here, the message triggers once two or more tabs are opened,
// even if the tabs were closed in between.
{
  trigger: { id: "nthTabOpened" },
  targeting: "tabsOpenedCount >= 2"
}
```
```js
// The trigger also tracks the number of tabs currently open,
// and the currentTabsOpen context variable can be used in targeting
// to ensure a minimum number of tabs are open.
// Here, the message will trigger on the next tab opened
// while 4 tabs remain open in the browser.
{
  trigger: { id: "nthTabOpened" },
  targeting: "currentTabsOpen >= 4"
}
```

### `tabGroupCreated`

Happens whenever a user creates a tab group.

```js
{
  trigger: { id: "tabGroupCreated" }
}
```
```js
// The trigger can also track the number of tab groups created in a
// session, by including the tabGroupsCreatedCount context variable in targeting.
// Here, the message triggers once two or more tab groups have been created,
// even if the tabs were closed in between.
{
  trigger: { id: "tabGroupCreated" },
  targeting: { "tabGroupsCreatedCount >= 2" }
}
```

### `tabGroupSaved`

Happens whenever a user uses the "Save and Close" action on a tab group.

```js
{
  trigger: { id: "tabGroupSaved" }
}
```
```js
// The trigger can also track the number of tab groups closed in a
// session, by including the tabGroupsClosedCount context variable in targeting.
// Here, the message triggers once two tab groups have been saved and closed.
{
  trigger: { id: "tabGroupSaved" },
  targeting: { "tabGroupsSavedCount >= 2" }
}
```

### `tabGroupCollapsed`

Happens whenever a user clicks a tab group label to collapse it. Can be used with the `currentTabGroups` targeting to ensure multiple groups have been collapsed.

```js
{
  trigger: { id: "tabGroupCollapsed" }
}
```
```js
// The trigger can also track the number of tab groups collapsed in a
// session, by including the tabGroupsCollapsedCount context variable in targeting.
// Here, the message triggers once four tab groups have been collapsed, or one group
// has been collapsed four times.
{
  trigger: { id: "tabGroupCollapsed" },
  targeting: { "tabGroupsCollapsedCount >= 4" }
}
```

### `activityAfterIdle`

Happens when the user resumes activity after n milliseconds of inactivity. Keyboard/mouse interactions and audio playback count as activity. The idle timer is reset when the OS is put to sleep or wakes from sleep.

No params or patterns. The `idleForMilliseconds` context variable is available in targeting. This value represents the number of milliseconds since the last user interaction or audio playback. `60000` is the minimum value for this variable (1 minute). In the following example, the message triggers when the user returns after at least 20 minutes of inactivity.

```js
// Register a message with the following trigger and include
// the idleForMilliseconds context variable in the targeting.
{
  trigger: { id: "activityAfterIdle" },
  targeting: "idleForMilliseconds >= 1200000"
}
```

### `cookieBannerDetected`

Happens when the `cookiebannerdetected` window event is dispatched. This event is dispatched when the following conditions are true:

1. The user is presented with a cookie consent banner on the webpage they're viewing,
2. The domain has a valid ruleset for automatically engaging with the consent banner, and
3. The user has not explicitly opted in or out of the Cookie Banner Handling feature.

### `cookieBannerHandled`

Happens when the `cookiebannerhandled` window event is dispatched. This event is dispatched when the following conditions are true:

1. The user is presented with a cookie consent banner on the webpage they're viewing,
2. The domain has a valid ruleset for automatically engaging with the consent banner, and
3. The user is opted into the Cookie Banner Handling feature (this is by default in private windows), and
4. Firefox succeeds in automatically engaging with the consent banner.

### `messagesLoaded`

Happens as soon as a message is loaded. This trigger does not require any user interaction, and may happen potentially as early as app launch, or at some time after experiment enrollment. Generally intended for use in reach experiments, because most messages cannot be routed unless the surfaces they display in are instantiated in a tabbed browser window (a reach message will not be displayed but its trigger will still be recorded). However, it is still possible to safely use this trigger for a normal message, with some caveats. This is potentially relevant on macOS, where the app can be running with no browser windows open, or even on Windows, where closing all browser windows but leaving open a non-browser window (e.g. the Library) causes the app to remain running.

A `toast_notification` or `update_action` message can function normally under these circumstances. A `toolbar_badge` message will load with or without a window, but will not actually display until a window exists. But messages with templates like `infobar` will have no effect unless a window exists to display them in. Any message using this trigger, regardless of template, can exclude window-less or browser-less contexts by adding the following targeting. This isn't strictly necessary because the messaging surfaces will either work normally or fail gracefully, but it may be desirable to test reach only in certain contexts, so the context objects `browser` and `browserWindow` are provided, corresponding to the selected browser (`gBrowser.selectedBrowser`) and the most recently active chrome window, respectively.

```js
{
  trigger: { id: "messagesLoaded" },
  targeting: "browser && browserWindow"
}
```

### `pageActionInUrlbar`

Happens when a page action appears in the location bar. The specific page action(s) to watch for can be specified by id in the targeting expression. For example, to trigger when the reader mode button appears:

```js
{
  trigger: { id: "pageActionInUrlbar" },
  targeting: "pageAction == 'reader-mode-button'"
}
```

### `onSearch`

Happens when the user uses the search feature in the awesome bar.

The `isSuggestion` boolean context variable is available in targeting, and will evaluate to true if the search was initiated from a recommendation in the awesomebar.

The `searchSource` string context variable is also available in targeting, and returns the search source. It will be one of four values: `urlbar-handoff` if one of the faux-search inputs were used (such as the one present on the newtab page), `urlbar-searchmode` if the user has selected a search engine, `urlbar-persisted` if the user has changed tabs or windows and come back to their search term in the URL bar, or `urlbar` if the user is doing a standard search by entering a term into the URL bar and pressing enter, or clicking on a search suggestion.

The `isOneOff` boolean context variable is available in targeting, and will be true if one of the one-off search features (typically found at the bottom of the awesomebar's dropdown menu) is used.

```js
{
  trigger: { id: "onSearch" },
  targeting: "isSuggestion && searchSource == 'urlbar-handoff' && isOneOff"
}
```

### `sidebarToolOpened`

Happens when the user opens a tool or extension panel in the sidebar

The `view` string context variable is available in targeting, and will correspond with which sidebar tool/extension has been opened (ex: "viewHistorySidebar", "viewBookmarksSidebar", etc).

The `clickCounts` object context variable is also available in targeting, and information about how many time a specific tool or extensions has been opened. The `SIDEBAR_TOOL_SURVEY` callout will be targeted to show if any tool/extension (excluding GenAI chatbot) has been clicked 5 times per-window and per-session. The `SIDEBAR_GENAI_SURVEY` callout will be targeted to show if the GenAI chatbot panel has been opened 2 times per-window and per-session.

```js
{
  trigger: { id: "sidebarToolOpened" },
  targeting: `'sidebar.position_start'|preferenceValue && view != 'viewGenaiChatSidebar' && clickCounts.totalToolsMinusGenai == 5 && !'messaging-system-action.sidebar-tools-microsurvey-complete-or-dismissed'|preferenceValue`
}
```

### `elementClicked`

Happens when an element in the browser chrome is clicked. The trigger will only fire if the element that is clicked has an ID that is within the trigger's params array.

The `elementId` string context variable is also available in targeting, and will correspond to the ID of the element that was clicked.

```js
{
  trigger: {
    id: "elementClicked",
    params: ["element1-id", "element2-id"]
  },
  targeting: "elementId == 'element1-id'"
}
```
