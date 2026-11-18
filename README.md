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

The dashboard includes the intended controls, but the following features are **not implemented yet**:

- Picking up/registering the active Chrome tab as a Zoom meeting.
- Building and persisting managed meeting cards from actual Zoom tabs.
- Activating a selected meeting.
- Muting or unmuting Zoom.
- Mute All / Release Active Meeting behavior.

The Pick Up Active Meeting Tab button explains this limitation when selected. Mute controls remain disabled. This is deliberate: the application must not claim a meeting is muted or unmuted unless it can read and verify the real Zoom state.

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
```

Change the Qt path to the location and version installed on your machine. The build uses the MSVC 2022 64-bit Qt kit.

Run the application:

```powershell
.\build\Release\MeetingController.exe
```

## First test

1. Open Chrome normally using your existing profile.
2. Join one Zoom meeting in Chrome.
3. Start Meeting Controller.
4. Wait for the UIA status in the lower-left sidebar to show **UIA Ready**.
5. Click **Live Diagnostics** in the left sidebar.
6. Click **Refresh** to enumerate visible Chrome windows.
7. Select the Chrome window containing the Zoom meeting.
8. Click **Inspect Selected Window**.
9. Review the **Accessibility Tree** tab.
10. Look for `TabItem`, `Document`, and button nodes related to Zoom audio/microphone controls.

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
- This build discovers top-level Chrome windows only; it does not yet pick up individual tabs or control microphones.

## Source layout

- `src/MainWindow.*` — Qt dashboard, left sidebar, dedicated diagnostics/settings/about pages.
- `src/ChromeWindowEnumerator.*` — Win32 enumeration of visible Chrome top-level windows.
- `src/UiaWorker.*` — COM/UIA worker thread and initialization.
- `src/UiaTreeInspector.*` — bounded Control View tree inspection and JSON serialization.
- `src/main.cpp` — Qt application entry point.

## References

- Microsoft UI Automation: https://learn.microsoft.com/windows/win32/winauto/entry-uiauto-win32
- Obtaining UI Automation elements: https://learn.microsoft.com/windows/win32/winauto/uiauto-obtainingelements
- Chrome Windows UIA support: https://developer.chrome.com/blog/windows-uia-support-update
- Chromium UIA provider documentation: https://chromium.googlesource.com/chromium/src/+/main/docs/accessibility/browser/uiautomation.md
