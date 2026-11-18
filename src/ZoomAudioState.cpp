#include "ZoomAudioState.h"

namespace
{
bool namesAction(const QString& text, const QString& action)
{
    return text == action || text.startsWith(action + " (");
}

QString microphoneState(const QString& text)
{
    for (const QString& action : {QString("mute"), QString("unmute")})
    {
        if (namesAction(text, action)
            || text.startsWith(action + " my audio")
            || text.startsWith(action + " microphone")
            || text.startsWith(action + " my microphone"))
            return action == "mute" ? "unmuted" : "muted";
    }
    return {};
}
}

namespace zoomAudio
{
Action actionForName(const QString& name)
{
    const QString text = name.simplified().toLower();
    for (const QString& action : {QString("join with computer audio"),
                                 QString("join computer audio"),
                                 QString("join audio by computer"),
                                 QString("connect to computer audio")})
    {
        if (namesAction(text, action))
            return Action::JoinComputerAudio;
    }
    for (const QString& action : {QString("leave computer audio"), QString("leave audio"),
                                 QString("disconnect computer audio"), QString("disconnect audio")})
    {
        if (namesAction(text, action))
            return Action::LeaveAudio;
    }
    if (namesAction(text, "join audio") || namesAction(text, "connect audio"))
        return Action::JoinAudio;

    if ((text.contains("audio") || text.contains("mute"))
        && (text.contains("option") || text.contains("menu")
            || text.contains("controls") || text.contains("settings")
            || text.contains("more")))
        return Action::OpenMenu;

    return Action::None;
}

Observation observe(const QStringList& visibleControlNames)
{
    Observation result;
    QString connectedControl;
    QString disconnectedControl;
    QString observedMicrophone;
    bool microphoneConflict = false;

    for (const QString& name : visibleControlNames)
    {
        switch (actionForName(name))
        {
        case Action::JoinAudio:
            result.joinAvailable = true;
            disconnectedControl = name;
            break;
        case Action::JoinComputerAudio:
            result.joinComputerAvailable = true;
            disconnectedControl = name;
            break;
        case Action::LeaveAudio:
            result.leaveAvailable = true;
            connectedControl = name;
            break;
        case Action::OpenMenu:
            result.menuAvailable = true;
            break;
        default:
        {
            const QString state = microphoneState(name.simplified().toLower());
            if (!state.isEmpty())
            {
                microphoneConflict = microphoneConflict
                    || (!observedMicrophone.isEmpty() && observedMicrophone != state);
                observedMicrophone = state;
                result.microphoneState = state;
                result.microphoneControlName = name;
            }
            break;
        }
        }
    }

    // Conflicting controls can occur during a render or in an unexpected Zoom
    // layout. Do not choose a state or perform a connection action in that case.
    if (!connectedControl.isEmpty() && disconnectedControl.isEmpty())
    {
        result.connection = Connection::Connected;
        result.controlName = connectedControl;
    }
    else if (connectedControl.isEmpty() && !disconnectedControl.isEmpty()
             && observedMicrophone.isEmpty())
    {
        result.connection = Connection::Disconnected;
        result.controlName = disconnectedControl;
        result.microphoneState = "audioNotConnected";
    }
    else
    {
        if (!connectedControl.isEmpty() || !disconnectedControl.isEmpty())
            microphoneConflict = true;
    }
    if (microphoneConflict)
    {
        result.microphoneState = "unknown";
        result.microphoneControlName.clear();
    }
    return result;
}

QString connectionName(Connection connection)
{
    switch (connection)
    {
    case Connection::Connected: return "connected";
    case Connection::Disconnected: return "disconnected";
    default: return "unknown";
    }
}

Result readConnection(const Backend& backend, int attempts)
{
    Result result;
    result.observation = backend.read();
    if (result.observation.connection == Connection::Unknown
        && result.observation.menuAvailable
        && backend.invoke(Action::OpenMenu))
    {
        for (int attempt = 0; attempt < attempts; ++attempt)
        {
            backend.wait();
            result.observation = backend.read();
            if (result.observation.connection != Connection::Unknown)
                break;
        }
    }
    result.ok = result.observation.connection != Connection::Unknown;
    if (!result.ok)
        result.error = "Zoom's computer-audio state could not be read. Open the meeting "
                       "toolbar and check Live Diagnostics for its audio controls.";
    return result;
}

Result setConnected(const Backend& backend, bool connected, int attempts)
{
    Result result = readConnection(backend, attempts);
    if (!result.ok)
        return result;

    const Connection wanted = connected ? Connection::Connected : Connection::Disconnected;
    if (result.observation.connection == wanted)
        return result;

    result.ok = false;
    if (!connected && !result.observation.leaveAvailable)
    {
        if (result.observation.menuAvailable && backend.invoke(Action::OpenMenu))
        {
            for (int attempt = 0; attempt < attempts; ++attempt)
            {
                backend.wait();
                result.observation = backend.read();
                if (result.observation.connection == wanted)
                {
                    result.ok = true;
                    return result;
                }
                if (result.observation.connection == Connection::Connected
                    && result.observation.leaveAvailable)
                    break;
            }
        }
        if (result.observation.connection != Connection::Connected
            || !result.observation.leaveAvailable)
        {
            result.error = "Zoom's Leave Computer Audio control could not be found. "
                           "The audio connection was not changed.";
            return result;
        }
    }

    const Action action = !connected ? Action::LeaveAudio
        : result.observation.joinComputerAvailable ? Action::JoinComputerAudio
        : result.observation.joinAvailable ? Action::JoinAudio : Action::None;
    if (action == Action::None)
    {
        result.error = "Zoom's Join Computer Audio control could not be found.";
        return result;
    }

    result.actionAttempted = true;
    if (!backend.invoke(action))
    {
        result.error = "Zoom's audio control could not be activated. The result is unverified.";
        return result;
    }

    bool computerJoinInvoked = action == Action::JoinComputerAudio;
    bool verificationMenuOpened = false;
    for (int attempt = 0; attempt < attempts; ++attempt)
    {
        backend.wait();
        result.observation = backend.read();
        if (result.observation.connection == wanted)
        {
            result.ok = true;
            result.changed = true;
            return result;
        }

        // Join Audio can first open a chooser. Activate the explicit computer
        // audio option once, then only observe; never keep clicking a toggle.
        if (connected && !computerJoinInvoked
            && result.observation.connection == Connection::Disconnected
            && result.observation.joinComputerAvailable)
        {
            computerJoinInvoked = true;
            if (!backend.invoke(Action::JoinComputerAudio))
            {
                result.error = "Zoom's Join Computer Audio option could not be activated. "
                               "The result is unverified.";
                return result;
            }
        }
        else if (connected && !verificationMenuOpened
                 && result.observation.connection == Connection::Unknown
                 && result.observation.menuAvailable)
        {
            // A microphone label alone does not prove computer-speaker audio.
            // Expose Leave Computer Audio to verify the actual connection.
            verificationMenuOpened = true;
            backend.invoke(Action::OpenMenu);
        }
    }

    result.error = "Zoom did not confirm the requested computer-audio state. "
                   "The result is unverified; check the meeting in Chrome.";
    return result;
}
}
