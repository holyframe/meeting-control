# Meeting Controller

**Project name:** `MeetingController`  
**Application name:** Meeting Controller  
**Version:** 0.4.0 — dashboard UI + Windows UI Automation feasibility build  
**Language:** C++20  
**UI:** Qt 6 Widgets  
**Browser integration:** Windows UI Automation (UIA)  
**Chrome extension:** None  
**Chrome DevTools Protocol:** None  
**Dedicated Chrome profile:** None

## UI design

The desktop interface follows the supplied dashboard reference:

- A fixed left sidebar for Dashboard, Pick Up Active Meeting Tab, Refresh Chrome Windows, Mute All Meetings, Live Diagnostics, Settings, and About.
- A dashboard with four summary cards: Total Meetings, Active Meeting, Safety Status, and UIA Connection.
- A meeting-card area in the main workspace.
- A dedicated Live Diagnostics page opened from the left sidebar. Diagnostics are not permanently displayed beside the dashboard.
- A dark navy/blue visual theme with Segoe UI typography.

The meeting list starts empty. The app does not display fabricated meetings or pretend to know a Zoom microphone state.

## What this build currently does

- Enumerates visible top-level windows owned by `chrome.exe`.
- Shows the number of detected Chrome windows on the dashboard.
- Provides a dedicated diagnostics page with a Chrome-window selector.
- Uses `IUIAutomation` to inspect a selected Chrome window's Control View tree.
- Reports accessible element names, control types, automation IDs, class names, enabled/offscreen state, bounds, and availability of common control patterns.
- Displays timestamped activity and inspection results.
- Runs UI Automation calls on a dedicated worker thread.
- Applies UI Automation connection/transaction timeouts where `IUIAutomation2` is available.
- Does not launch Chrome, modify Chrome profiles, use CDP, install an extension, or change any meeting state.

## Important: current build is read-only

**Pick Up Meeting Tabs** lists Google Chrome browser tabs, grouped by Chrome profile, and lets you check one or more to import. Chromium and other Chromium-based browsers are not listed, and neither are Chrome windows that have no tab strip. Importing a tab selects it, brings its window forward, and reads the Zoom microphone, so the card starts out active and shows Mute or Unmute. Chrome exposes a page address only for the tab currently shown in each window. When that address is a Zoom web-client meeting (`/wc/<meeting id>` on `zoom.us`, `zoom.com`, or `zoomgov.com`), the meeting ID is stored. Only the host and meeting ID are kept; the full URL is not stored or logged because it can contain the passcode. Managed meetings are kept in memory only.

Each managed meeting card offers:

- **Mute / Unmute** — shown from the microphone state read at import. It reads the control again, acts only when the current state differs, then re-reads the control until Zoom reports the new state.
- **⋯** — refresh the microphone state or remove the meeting from the list.

**Mute All Meetings** applies the same read-act-verify sequence to every managed meeting in turn. **Release Active Meeting** only clears the active marker in this app; it does not change Zoom.

A card never shows a microphone state that was not read back from Zoom. If a change cannot be verified, the state is reset to unread and the failure is reported.

The following features are **not implemented yet**:

- Persisting managed meetings across restarts.
- Watching for microphone changes made in Zoom itself; states refresh only when this app reads them.

## UI Automation validation gate

Before implementing meeting pickup or microphone control, confirm that the user's Chrome + Zoom Web App accessibility tree exposes:

1. The Chrome tab strip and currently selected `TabItem`.
2. The active Zoom page/document.
3. The Zoom microphone control.
4. A reliable state indicator (for example, accessible name or `TogglePattern` state).
5. A supported action pattern (for example, `InvokePattern` or `TogglePattern`).

Do not add blind toggling or coordinate-based clicking. Any future microphone operation must read the current state, perform the action only when needed, and read the state again to verify the result.

## Requirements

- Windows 10 or Windows 11, 64-bit.
- Visual Studio 2022 with **Desktop development with C++**.
- CMake 3.24 or newer.
- Qt 6.5 or newer, MSVC 2022 64-bit kit.
- Google Chrome.

## Build

Open **Developer PowerShell for Visual Studio 2022** and run:

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH="C:\Qt\6.8.3\msvc2022_64"
cmake --build build --config Release
& "C:\Qt\6.8.3\msvc2022_64\bin\windeployqt.exe" --release .\build\Release\MeetingController.exe
```

`build.ps1` runs these three steps. Change the Qt path to the location and version installed on your machine. The build uses the MSVC 2022 64-bit Qt kit. `windeployqt` copies `Qt6Core.dll`, `Qt6Gui.dll`, `Qt6Widgets.dll`, and the Windows platform plugin next to the executable so it can be launched directly.

Run the application:

```powershell
.\build\Release\MeetingController.exe
```

## First test

1. Open Chrome normally using your existing profile.
2. Join one Zoom meeting in Chrome.
3. Start Meeting Controller. Nothing is scanned or initialized at startup.
4. Click **Live Diagnostics** in the left sidebar.
5. Click **Refresh** to enumerate visible Chrome windows.
6. Select the Chrome window containing the Zoom meeting.
7. Click **Inspect Selected Window**. Windows UI Automation is initialized on the first inspection, and the sidebar status changes to **UIA Ready**.
8. Review the **Accessibility Tree** tab.
9. Look for `TabItem`, `Document`, and button nodes related to Zoom audio/microphone controls.

The selector lists Chrome windows, not individual tabs. The app does not yet select or register a tab.

## What to share for the next implementation phase

Share the diagnostics output for the Chrome window containing the live Zoom meeting. If the output is large, include the sections around:

- Chrome's tab strip and `TabItem` elements.
- The web content `Document` element.
- Buttons whose names mention microphone, mute, audio, or unmute.

Redact meeting titles, participant names, and other private details before sharing.

## Known limitations

- Chrome's UIA provider exposes accessibility data, not the complete DOM.
- UIA may expose the browser tab strip and only the active tab's page content. Background-tab page controls may not be available until that tab is selected. This must be tested on the actual Chrome build.
- UIA control patterns vary by element. A visible button may expose `InvokePattern`, `LegacyIAccessiblePattern`, or no actionable pattern.
- The accessibility tree can change with Chrome and Zoom updates.
- UIA cannot bypass Zoom's server-side simultaneous-meeting restrictions.
- Background Chrome tabs are listed by title. Their page address is available only for the tab currently shown in that window.
- Chrome renders only the selected tab of each window, so a meeting tab is selected before its microphone is read or changed. Controlling several meetings therefore switches tabs in Chrome.
- The microphone control is identified by its accessible name (`Mute`, `Unmute`, or a name mentioning the microphone). A Zoom redesign that renames the control would break detection, which is reported as "no microphone control was found" rather than guessed at.
- Meetings that have not joined audio expose a Join Audio control instead; these are reported and left unchanged.

## Source layout

- `src/MainWindow.*` — Qt dashboard, left sidebar, dedicated diagnostics/settings/about pages.
- `src/ChromeWindowEnumerator.*` — Win32 enumeration of visible Chrome top-level windows.
- `src/UiaWorker.*` — COM/UIA worker thread and initialization.
- `src/UiaTreeInspector.*` — bounded Control View tree inspection and JSON serialization.
- `src/ActiveTabPicker.*` — lists Chrome tabs for import.
- `src/ZoomMeetingController.*` — tab activation and read/act/verify microphone control.
- `src/UiaSupport.*` — shared UIA property reads and Chrome browser-UI scanning.
- `src/main.cpp` — Qt application entry point.

## References

- Microsoft UI Automation: https://learn.microsoft.com/windows/win32/winauto/entry-uiauto-win32
- Obtaining UI Automation elements: https://learn.microsoft.com/windows/win32/winauto/uiauto-obtainingelements
- Chrome Windows UIA support: https://developer.chrome.com/blog/windows-uia-support-update
- Chromium UIA provider documentation: https://chromium.googlesource.com/chromium/src/+/main/docs/accessibility/browser/uiautomation.md
