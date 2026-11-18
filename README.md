# Meeting Controller

A native Windows C++ application that launches a dedicated Chrome profile and communicates with it through the Chrome DevTools Protocol (CDP) over inherited Windows pipes.

**This is a Phase 0/Phase 1 feasibility starter.** It launches managed Chrome, discovers page targets, and inspects visible buttons in a selected page. It intentionally does **not** click Zoom microphone controls yet. The exact Zoom Web App DOM and mute-state semantics must be inspected and validated before enabling microphone automation.

## Project name

`Meeting Controller`

## Current capabilities

- Native Windows desktop UI using Qt 6 Widgets.
- Launches a dedicated, persistent Chrome user-data directory.
- Uses `--remote-debugging-pipe=asciiz` and `--remote-debugging-io-pipes` on Windows.
- Does not expose a remote-debugging TCP port.
- Discovers Chrome page targets through `Target.getTargets`.
- Lets the user select a tab and inspect its visible buttons using `Runtime.evaluate`.
- Displays the inspection result as JSON.
- Closes the managed Chrome process when the application exits after confirmation.

## Not implemented yet

- Zoom microphone mute/unmute.
- Exclusive active-meeting coordination.
- Multi-profile / multi-Chrome-instance management.
- Global hotkeys.
- Speaker control.
- Emergency microphone endpoint mute.

Those features are deliberately deferred until the actual Zoom DOM and independent meeting-session behavior are validated. Blindly clicking a guessed selector would be unsafe.

## Requirements

- Windows 10 or Windows 11, 64-bit.
- Visual Studio 2022 with the Desktop development with C++ workload.
- CMake 3.24 or later.
- Qt 6.5 or later, MSVC 2022 64-bit kit.
- Google Chrome installed.

No third-party JSON library is required; the project uses Qt's JSON classes.

## Build

Open a Developer PowerShell for VS 2022.

Set `CMAKE_PREFIX_PATH` to your Qt MSVC installation. Example:

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH="C:\Qt\6.8.3\msvc2022_64"
cmake --build build --config Release
```

Run:

```powershell
.\build\Release\MeetingController.exe
```

If your Qt installation is in a different location, change `CMAKE_PREFIX_PATH`.

## First run

1. Start Meeting Controller.
2. Click **Launch Managed Chrome**.
3. Chrome opens with a dedicated profile under the current user's local application data directory:
   `%LOCALAPPDATA%\MeetingController\chrome-profile`
4. Sign in to Zoom in this managed Chrome profile if needed.
5. Join a test meeting.
6. Return to Meeting Controller and click **Refresh Tabs**.
7. Select the Zoom tab.
8. Click **Inspect Selected Tab**.
9. Review the JSON inventory of visible buttons in the output panel.

The application does not use your normal Chrome profile. This is intentional: Chrome 136+ requires remote debugging to use a non-default user-data directory.

## Important behavior

- Use the Chrome window launched by Meeting Controller for meetings you want it to manage.
- Existing Chrome windows that were launched normally are not controlled by this starter.
- The dedicated Chrome profile persists between runs, but the managed browser process is closed when Meeting Controller exits.
- Closing the managed browser disconnects meetings open in that browser. The app asks for confirmation first.
- A separate Chrome profile does not bypass Zoom's simultaneous-meeting/account restrictions.

## CDP pipe details

On Windows, Chromium accepts two inherited handles through:

```text
--remote-debugging-pipe=asciiz
--remote-debugging-io-pipes=<browser-read-handle>,<browser-write-handle>
```

The application creates two anonymous pipes and passes only the child-side handles using `PROC_THREAD_ATTRIBUTE_HANDLE_LIST`. The parent communicates with Chrome using the remaining pipe ends. Each CDP JSON message is terminated with a NUL byte.

This is based on Chromium's Windows ChromeDriver pipe setup:
- https://chromium.googlesource.com/chromium/src/+/main/chrome/test/chromedriver/net/pipe_builder.cc
- https://chromium.googlesource.com/chromium/src/+/main/content/public/common/content_switches.cc

Chrome remote debugging security change:
- https://developer.chrome.com/blog/remote-debugging-port

## Safety note

CDP is a powerful browser-control interface. This application intentionally uses a dedicated profile and inherited pipes instead of opening a remote-debugging TCP port. Do not change this to `--remote-debugging-port=9222` or expose a DevTools endpoint to your network.

The button inventory is read-only. Do not enable microphone automation until the Zoom button's labels, state transitions, and behavior have been verified against the exact Zoom Web App version in use.

## Next development milestone

Capture the button inventory from a real Zoom meeting in the managed Chrome profile. Use that evidence to implement a versioned `ZoomWebAdapter` that:

1. Identifies the actual microphone control.
2. Reads muted/unmuted state without guessing.
3. Sets an explicit desired state rather than toggling blindly.
4. Verifies the resulting state.
5. Fails closed when state is unknown.

Only after that should the `AudioCoordinator` and multi-meeting safety state machine be implemented.
