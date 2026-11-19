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

- A top page bar for Dashboard, Live Diagnostics, Settings, and About. Pick Up and Mute All stay on the dashboard.
- A dashboard summary row: Mute All Meetings, Meeting Counts (active/total), Sound Off All, UIA Connection, and Pick Up Meeting Tabs. Refresh inside UIA Connection re-reads every meeting's computer-audio connection and updates which cards are active. A meeting card is active when computer audio is connected or the meeting is ready to join it.
- A meeting-card area in the main workspace.
- A dedicated Live Diagnostics page opened from the top page bar. Diagnostics are not permanently displayed beside the dashboard.
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
- Controls the microphone and joins/leaves computer audio through Zoom's accessible controls, verifying each result.
- Does not launch Chrome, modify Chrome profiles, use CDP, or install an extension.

## Meeting controls

**Pick Up Meeting Tabs** lists Google Chrome browser tabs, grouped by Chrome profile, and lets you check one or more to import. Chromium and other Chromium-based browsers are not listed, and neither are Chrome windows that have no tab strip. Importing a tab selects it, brings its window forward, and reads the Zoom microphone and computer-audio connection. Chrome exposes a page address only for the tab currently shown in each window. When that address is a Zoom web-client meeting (`/wc/<meeting id>` on `zoom.us`, `zoom.com`, or `zoomgov.com`), the meeting ID is stored. Only the host and meeting ID are kept; the full URL is not stored or logged because it can contain the passcode. Managed meetings are kept in memory only.

Each managed meeting card offers:

- **Mute / Unmute** — shown from the microphone state read at import. It reads the control again, acts only when the current state differs, then re-reads the control until Zoom reports the new state.
- **Sound Off** — opens Zoom's audio menu if needed, chooses **Leave Computer Audio**, and verifies that Zoom exposes a join-audio control. This disconnects the microphone as well as incoming sound, while leaving the meeting and video running.
- **Sound On** brings the meeting window forward and uses the audio chooser Zoom may have opened when you clicked **Sound Off**. It selects **Computer Audio** if needed, then activates **Join Audio by Computer** once. When Windows identifies the visible button under the pointer target, the controller moves the pointer there and clicks it. The card shows **Sound on** after Zoom exposes **Leave Computer Audio**. If you already rejoined manually, pressing **Sound On** just verifies the connection and refreshes the card.
- **Read Sound** — shown when the connection state has not been read or could not be verified. Reads the state without joining/leaving audio.
- **×** — remove the meeting from this list. Zoom is not changed.
- The card name can be edited. That name is only for this list; the Chrome tab title is unchanged.
- Double-click a card to bring its Chrome tab forward. Drag a card onto another card to change their order.
- **⟳** on the left of the Active label — read the microphone and sound states from Zoom again. Unmute is 1.5 times as wide as Sound On or Sound Off.

Sound refers to Zoom's computer-audio connection, not a separate speaker mute or the Windows volume. **Join Audio** confirms disconnected computer audio; **Leave Computer Audio** confirms a connection, opening the audio menu to read it when necessary. Microphone labels are read separately and cannot by themselves verify sound-on. Conflicting or unrecognized controls are treated as unknown. No sound is inferred from silence or from a successful click alone.

**Mute All Meetings** applies the same read-act-verify sequence to every managed meeting in turn.

**Sound Off All** leaves computer audio for every managed meeting. Sound turns off and the microphone disconnects. Chrome switches to each meeting tab, and each result is verified.

A card shows the last audio states read back from Zoom. If a sound change cannot be verified, both the sound and microphone states are reset to unread and the failure is reported. The controller stops if its meeting tab is no longer selected. A pointer click requires a uniquely identified, enabled join button in the foreground meeting. Windows must confirm that the computed screen point belongs to that button or its text. The controller can also invoke the identified button through its accessibility action.

The following features are **not implemented yet**:

- Persisting managed meetings across restarts.
- Watching for microphone or computer-audio changes made in Zoom itself; states refresh only when this app reads them.

## UI Automation validation gate

Validate control discovery against the user's Chrome + Zoom Web App accessibility tree:

1. The Chrome tab strip and currently selected `TabItem`.
2. The active Zoom page/document.
3. The Zoom microphone control.
4. A reliable state indicator (for example, accessible name or `TogglePattern` state).
5. A supported action pattern (for example, `InvokePattern` or `TogglePattern`).
6. The audio menu's **Leave Computer Audio** option and the disconnected **Join Audio** / **Join with Computer Audio** controls.

Every microphone or sound operation must read the current state, perform the action only when needed, and read the state again to verify the result. The current implementation invokes accessible default actions and can expand an accessible audio menu.

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
4. Click **Live Diagnostics** in the top page bar.
5. Click **Refresh** to enumerate visible Chrome windows.
6. Select the Chrome window containing the Zoom meeting.
7. Click **Inspect Selected Window**. Windows UI Automation is initialized on the first inspection, and the dashboard UIA card changes to **Ready**.
8. Review the **Accessibility Tree** tab.
9. Look for `TabItem`, `Document`, and button nodes related to Zoom audio/microphone controls.

The diagnostics selector lists Chrome windows. Use **Pick Up Meeting Tabs** on the dashboard to import individual tabs.

## Sound-control validation

1. Import a Zoom web meeting with computer audio connected. Confirm the card reads **Sound on** and shows **Sound Off**.
2. Click **Sound Off**. Confirm Zoom leaves computer audio, the card reads **Sound off** and **Mic disconnected**, and the action changes to **Sound On**.
3. Click **Sound On**. Confirm any computer-audio chooser is handled, the card reads **Sound on**, and the microphone matches Zoom's actual state.
4. Use **Refresh sound state** after changing audio directly in Zoom.
5. Verify a second meeting remains unchanged. Repeatedly requesting an already-established state must not click the join/leave control again.
6. Hide/unavailable controls, a closed tab, or an unverified operation must produce an unread state and diagnostics rather than assumed success.

Reconnect allows 50 verification reads with a 200 ms wait between reads to accommodate Zoom's audio setup. Live Diagnostics records the controls observed and each join/selection action, including elapsed time. A chooser still visible after a join request is reported as unverified. Control properties are fetched together in a fresh UIA snapshot so polling does not read each property separately across processes.

Automated state-transition and card tests run without accessing Chrome or any real meeting:

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH="C:\Qt\6.8.3\msvc2022_64" -DMEETING_CONTROLLER_BUILD_TESTS=ON
cmake --build build --config Release
$env:PATH = "C:\Qt\6.8.3\msvc2022_64\bin;" + $env:PATH
ctest --test-dir build -C Release --output-on-failure
```

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
- The sound button joins/leaves computer audio and therefore affects microphone connectivity too. It does not provide speaker-only muting.
- Audio detection currently recognizes English control names. Different Zoom layouts, localized labels, unavailable action patterns, phone audio, or permission dialogs may require manual action in Zoom and a fresh read.

## Source layout

- `src/MainWindow.*` — Qt dashboard, top page bar, dedicated diagnostics/settings/about pages.
- `src/ChromeWindowEnumerator.*` — Win32 enumeration of visible Chrome top-level windows.
- `src/UiaWorker.*` — COM/UIA worker thread and initialization.
- `src/UiaTreeInspector.*` — bounded Control View tree inspection and JSON serialization.
- `src/ActiveTabPicker.*` — lists Chrome tabs for import.
- `src/ZoomMeetingController.*` — tab activation and UIA adapters for microphone and computer-audio control.
- `src/ZoomAudioState.*` — audio-control recognition and read/act/verify connection transitions.
- `tests/MeetingControllerTests.cpp` — simulated audio transitions and offscreen card tests.
- `src/UiaSupport.*` — shared UIA property reads and Chrome browser-UI scanning.
- `src/main.cpp` — Qt application entry point.

## References

- Microsoft UI Automation: https://learn.microsoft.com/windows/win32/winauto/entry-uiauto-win32
- Obtaining UI Automation elements: https://learn.microsoft.com/windows/win32/winauto/uiauto-obtainingelements
- Chrome Windows UIA support: https://developer.chrome.com/blog/windows-uia-support-update
- Chromium UIA provider documentation: https://chromium.googlesource.com/chromium/src/+/main/docs/accessibility/browser/uiautomation.md
