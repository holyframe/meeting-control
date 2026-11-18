#include "ZoomMeetingController.h"

#include "UiaSupport.h"
#include "ZoomAudioState.h"

#include <QThread>

#include <OleAuto.h>
#include <deque>

using Microsoft::WRL::ComPtr;

namespace
{
// Zoom re-renders the control after a click, so the new state is polled rather
// than read once.
constexpr int kVerifyAttempts = 15;
constexpr int kVerifyIntervalMs = 200;
constexpr int kSelectSettleMs = 250;

QJsonObject failure(const QString& error)
{
    return QJsonObject{
        {"ok", false},
        {"error", error}
    };
}

// What the microphone control says about the meeting, named for the state Zoom
// is in rather than for the action the button offers.
enum class Microphone
{
    Unknown,
    Muted,
    Unmuted,
    AudioNotConnected
};

QString microphoneName(Microphone state)
{
    switch (state)
    {
    case Microphone::Muted: return "muted";
    case Microphone::Unmuted: return "unmuted";
    case Microphone::AudioNotConnected: return "audioNotConnected";
    default: return "unknown";
    }
}

// Zoom labels the button with the action it performs, so "Unmute" means the
// microphone is currently muted.
Microphone classifyButton(const QString& name)
{
    const QString text = name.trimmed().toLower();

    if (text.isEmpty())
        return Microphone::Unknown;

    if (text.startsWith("unmute"))
        return Microphone::Muted;

    // "Mute All" in the participants panel acts on everyone else.
    if (text.startsWith("mute") && !text.startsWith("mute all"))
        return Microphone::Unmuted;

    if (text.contains("join audio") || text.contains("connect audio"))
        return Microphone::AudioNotConnected;

    return Microphone::Unknown;
}

// A button named exactly "Mute" or "Unmute", or one that names the microphone,
// is the meeting control. Anything else that merely mentions muting is a weaker
// match and is only used when nothing better is found.
int matchStrength(const QString& name)
{
    const QString text = name.trimmed().toLower();

    if (text == "mute" || text == "unmute")
        return 3;

    if (text.contains("microphone") || text.contains("my audio"))
        return 2;

    return 1;
}

bool invokeElement(IUIAutomationElement* element)
{
    ComPtr<IUIAutomationInvokePattern> invoke;
    if (SUCCEEDED(element->GetCurrentPatternAs(
            UIA_InvokePatternId, IID_PPV_ARGS(invoke.GetAddressOf())))
        && invoke)
    {
        return SUCCEEDED(invoke->Invoke());
    }

    ComPtr<IUIAutomationLegacyIAccessiblePattern> legacy;
    if (SUCCEEDED(element->GetCurrentPatternAs(
            UIA_LegacyIAccessiblePatternId, IID_PPV_ARGS(legacy.GetAddressOf())))
        && legacy)
    {
        return SUCCEEDED(legacy->DoDefaultAction());
    }

    return false;
}

bool isVisibleEnabled(IUIAutomationElement* element)
{
    BOOL offscreen = TRUE;
    BOOL enabled = FALSE;
    return element
        && SUCCEEDED(element->get_CurrentIsOffscreen(&offscreen)) && !offscreen
        && SUCCEEDED(element->get_CurrentIsEnabled(&enabled)) && enabled;
}

bool isAudioControlType(CONTROLTYPEID type)
{
    return type == UIA_ButtonControlTypeId || type == UIA_MenuItemControlTypeId
        || type == UIA_SplitButtonControlTypeId;
}
}

ZoomMeetingController::ZoomMeetingController(IUIAutomation* automation)
{
    automation_ = automation;
}

ComPtr<IUIAutomationElement> ZoomMeetingController::rootOf(HWND hwnd) const
{
    ComPtr<IUIAutomationElement> root;

    if (automation_ && IsWindow(hwnd))
        automation_->ElementFromHandle(hwnd, root.GetAddressOf());

    return root;
}

ComPtr<IUIAutomationElement> ZoomMeetingController::findTab(
    const ComPtr<IUIAutomationElement>& root,
    const QString& tabRuntimeId,
    const QString& tabName) const
{
    ComPtr<IUIAutomationTreeWalker> walker;
    if (FAILED(automation_->get_ControlViewWalker(walker.GetAddressOf())) || !walker)
        return {};

    const uia::BrowserUi ui = uia::scanBrowserUi(walker.Get(), root.Get());

    for (const auto& item : ui.tabItems)
    {
        if (!tabRuntimeId.isEmpty() && uia::readRuntimeId(item.Get()) == tabRuntimeId)
            return item;
    }

    if (tabName.isEmpty())
        return {};

    // Chrome rebuilds tab elements when tabs are dragged or reordered, so the
    // runtime ID can go stale while the tab is still open.
    ComPtr<IUIAutomationElement> titleMatch;
    for (const auto& item : ui.tabItems)
    {
        if (uia::readStringProperty(item.Get(), UIA_NamePropertyId) == tabName)
        {
            if (titleMatch)
                return {}; // Duplicate titles cannot identify a meeting safely.
            titleMatch = item;
        }
    }
    return titleMatch;
}

ComPtr<IUIAutomationElement> ZoomMeetingController::findLiveDocument(HWND hwnd) const
{
    ComPtr<IUIAutomationElement> root = rootOf(hwnd);
    if (!root)
        return {};

    ComPtr<IUIAutomationTreeWalker> walker;
    if (FAILED(automation_->get_ControlViewWalker(walker.GetAddressOf())) || !walker)
        return {};

    const uia::BrowserUi ui = uia::scanBrowserUi(walker.Get(), root.Get());

    for (const auto& document : ui.documents)
    {
        if (!uia::readBooleanProperty(document.Get(), UIA_IsOffscreenPropertyId))
            return document;
    }

    return {};
}

ComPtr<IUIAutomationElement> ZoomMeetingController::findMicrophoneButton(
    IUIAutomationElement* document) const
{
    VARIANT buttonType;
    VariantInit(&buttonType);
    buttonType.vt = VT_I4;
    buttonType.lVal = UIA_ButtonControlTypeId;

    ComPtr<IUIAutomationCondition> condition;
    const HRESULT conditionHr = automation_->CreatePropertyCondition(
        UIA_ControlTypePropertyId, buttonType, condition.GetAddressOf());
    VariantClear(&buttonType);

    if (FAILED(conditionHr) || !condition)
        return {};

    ComPtr<IUIAutomationElementArray> buttons;
    if (FAILED(document->FindAll(
            TreeScope_Descendants, condition.Get(), buttons.GetAddressOf()))
        || !buttons)
        return {};

    int count = 0;
    buttons->get_Length(&count);

    ComPtr<IUIAutomationElement> best;
    int bestStrength = 0;

    for (int index = 0; index < count; ++index)
    {
        ComPtr<IUIAutomationElement> button;
        if (FAILED(buttons->GetElement(index, button.GetAddressOf())) || !button)
            continue;

        const QString name = uia::readStringProperty(button.Get(), UIA_NamePropertyId);
        if (classifyButton(name) == Microphone::Unknown)
            continue;

        const int strength = matchStrength(name);
        if (strength > bestStrength)
        {
            best = button;
            bestStrength = strength;
        }
    }

    return best;
}

QJsonObject ZoomMeetingController::readMicrophoneInWindow(HWND hwnd) const
{
    ComPtr<IUIAutomationElement> document = findLiveDocument(hwnd);
    if (!document)
    {
        return failure(
            "The page content of that tab is not available through UI Automation.");
    }

    ComPtr<IUIAutomationElement> button = findMicrophoneButton(document.Get());
    if (!button)
    {
        return failure(
            "No microphone control was found on that page. Open the meeting tab and "
            "make sure the Zoom toolbar is visible.");
    }

    const QString name = uia::readStringProperty(button.Get(), UIA_NamePropertyId);

    return QJsonObject{
        {"ok", true},
        {"state", microphoneName(classifyButton(name))},
        {"controlName", name}
    };
}

QJsonObject ZoomMeetingController::activateTab(
    quintptr hwndValue,
    const QString& tabRuntimeId,
    const QString& tabName,
    bool bringToFront) const
{
    if (!automation_)
        return failure("UI Automation is not initialized.");

    HWND hwnd = reinterpret_cast<HWND>(hwndValue);
    if (!IsWindow(hwnd))
        return failure("That Chrome window is no longer open.");

    ComPtr<IUIAutomationElement> root = rootOf(hwnd);
    if (!root)
        return failure("The Chrome window could not be read through UI Automation.");

    ComPtr<IUIAutomationElement> tab = findTab(root, tabRuntimeId, tabName);
    if (!tab)
        return failure("That tab is no longer open in its Chrome window.");

    bool selected = uia::readBooleanProperty(
        tab.Get(), UIA_SelectionItemIsSelectedPropertyId);

    ComPtr<IUIAutomationSelectionItemPattern> selection;
    if (SUCCEEDED(tab->GetCurrentPatternAs(
            UIA_SelectionItemPatternId, IID_PPV_ARGS(selection.GetAddressOf())))
        && selection)
    {
        if (FAILED(selection->Select()))
            return failure("Chrome refused to switch to that tab.");

        QThread::msleep(kSelectSettleMs);
        selected = uia::readBooleanProperty(
            tab.Get(), UIA_SelectionItemIsSelectedPropertyId);
    }

    if (bringToFront)
    {
        if (IsIconic(hwnd))
            ShowWindow(hwnd, SW_RESTORE);

        SetForegroundWindow(hwnd);
    }

    if (!selected)
        return failure("The tab did not become the selected tab in its window.");

    return QJsonObject{
        {"ok", true},
        {"tabRuntimeId", uia::readRuntimeId(tab.Get())},
        {"tabName", uia::readStringProperty(tab.Get(), UIA_NamePropertyId)}
    };
}

QJsonObject ZoomMeetingController::activateAndReadMicrophone(
    quintptr hwndValue,
    const QString& tabRuntimeId,
    const QString& tabName) const
{
    const QJsonObject activation =
        activateTab(hwndValue, tabRuntimeId, tabName, true);

    if (!activation.value("ok").toBool())
        return activation;

    QJsonObject result = readMicrophoneInWindow(reinterpret_cast<HWND>(hwndValue));
    const QJsonObject audio = readAudioConnection(
        hwndValue, activation.value("tabRuntimeId").toString(), tabName);
    if (audio.value("ok").toBool())
        result = audio;
    else
    {
        // A missing audio-menu name must not discard an independently readable
        // microphone during import. Sound stays unread until it can be verified.
        result.insert("audioState", "unknown");
        result.insert("audioError", audio.value("error"));
    }
    result.insert("activated", true);
    result.insert("tabRuntimeId", audio.value("tabRuntimeId").toString().isEmpty()
        ? activation.value("tabRuntimeId") : audio.value("tabRuntimeId"));
    result.insert("tabName", activation.value("tabName"));
    return result;
}

std::vector<ZoomMeetingController::Control> ZoomMeetingController::collectControls(
    IUIAutomationElement* root) const
{
    std::vector<Control> controls;
    if (!automation_ || !root)
        return controls;

    ComPtr<IUIAutomationCondition> condition;
    for (const CONTROLTYPEID type : {UIA_ButtonControlTypeId, UIA_MenuItemControlTypeId,
                                     UIA_SplitButtonControlTypeId})
    {
        VARIANT value;
        VariantInit(&value);
        value.vt = VT_I4;
        value.lVal = type;
        ComPtr<IUIAutomationCondition> typeCondition;
        if (FAILED(automation_->CreatePropertyCondition(
                UIA_ControlTypePropertyId, value, typeCondition.GetAddressOf())))
            return {};
        if (!condition)
            condition = typeCondition;
        else
        {
            ComPtr<IUIAutomationCondition> combined;
            if (FAILED(automation_->CreateOrCondition(
                    condition.Get(), typeCondition.Get(), combined.GetAddressOf())))
                return {};
            condition = combined;
        }
    }

    ComPtr<IUIAutomationElementArray> elements;
    if (FAILED(root->FindAll(TreeScope_Descendants, condition.Get(), elements.GetAddressOf()))
        || !elements)
        return controls;

    int count = 0;
    elements->get_Length(&count);
    for (int index = 0; index < count && index < 2000; ++index)
    {
        ComPtr<IUIAutomationElement> element;
        if (SUCCEEDED(elements->GetElement(index, element.GetAddressOf()))
            && isVisibleEnabled(element.Get()))
        {
            controls.push_back({element,
                uia::readStringProperty(element.Get(), UIA_NamePropertyId)});
        }
    }
    return controls;
}

std::vector<ZoomMeetingController::Control> ZoomMeetingController::collectWindowControls(
    HWND hwnd) const
{
    std::vector<Control> controls;
    ComPtr<IUIAutomationTreeWalker> walker;
    const auto root = rootOf(hwnd);
    if (!root || FAILED(automation_->get_ControlViewWalker(walker.GetAddressOf())) || !walker)
        return controls;

    // Only collect popup controls outside the page. Do not scan other document
    // subtrees or mistake a browser toolbar button for a Zoom audio control.
    struct Pending { ComPtr<IUIAutomationElement> element; bool popup; };
    std::deque<Pending> pending;
    pending.push_back({root, false});
    int visited = 0;
    while (!pending.empty() && visited++ < 1200)
    {
        const Pending current = std::move(pending.front());
        pending.pop_front();
        CONTROLTYPEID type = 0;
        current.element->get_CurrentControlType(&type);
        if (type == UIA_DocumentControlTypeId)
            continue;

        const QString role = uia::readStringProperty(current.element.Get(), UIA_AriaRolePropertyId);
        const bool popup = current.popup || type == UIA_MenuControlTypeId
            || (current.element != root && type == UIA_WindowControlTypeId)
            || role == "dialog" || role == "menu";
        if ((popup || type == UIA_MenuItemControlTypeId) && isAudioControlType(type)
            && isVisibleEnabled(current.element.Get()))
        {
            controls.push_back({current.element,
                uia::readStringProperty(current.element.Get(), UIA_NamePropertyId)});
        }

        ComPtr<IUIAutomationElement> child;
        walker->GetFirstChildElement(current.element.Get(), child.GetAddressOf());
        int siblings = 0;
        while (child && siblings++ < 1200 && pending.size() < 1200)
        {
            pending.push_back({child, popup});
            ComPtr<IUIAutomationElement> next;
            if (FAILED(walker->GetNextSiblingElement(child.Get(), next.GetAddressOf())))
                break;
            child = std::move(next);
        }
    }
    return controls;
}

QJsonObject ZoomMeetingController::controlAudioConnection(
    quintptr hwndValue, const QString& tabRuntimeId, const QString& tabName,
    std::optional<bool> connected) const
{
    const QJsonObject activation = activateTab(hwndValue, tabRuntimeId, tabName, false);
    if (!activation.value("ok").toBool())
        return activation;

    const HWND hwnd = reinterpret_cast<HWND>(hwndValue);
    const QString selectedId = activation.value("tabRuntimeId").toString();
    QString contextError;
    bool targetLost = false;
    bool searchPopups = false;
    bool openedMenu = false;
    QString menuRuntimeId;
    QString menuName;

    const auto targetSelected = [&]
    {
        if (targetLost)
            return false;
        const auto root = rootOf(hwnd);
        const auto tab = root ? findTab(root, selectedId, QString()) : nullptr;
        if (!tab || !uia::readBooleanProperty(tab.Get(), UIA_SelectionItemIsSelectedPropertyId))
        {
            targetLost = true;
            contextError = "The meeting tab closed or is no longer selected. "
                           "The sound result is unverified; check the meeting in Chrome.";
            return false;
        }
        return true;
    };
    const auto readControls = [&]
    {
        std::vector<Control> controls;
        if (!targetSelected())
            return controls;
        const auto document = findLiveDocument(hwnd);
        if (document)
            controls = collectControls(document.Get());
        if (searchPopups)
        {
            auto popupControls = collectWindowControls(hwnd);
            controls.insert(controls.end(), popupControls.begin(), popupControls.end());
        }
        if (!targetSelected())
            return std::vector<Control>();
        return controls;
    };
    const auto uniqueControl = [&](const std::vector<Control>& controls, zoomAudio::Action action)
    {
        ComPtr<IUIAutomationElement> match;
        for (const Control& control : controls)
        {
            if (zoomAudio::actionForName(control.name) != action)
                continue;
            CONTROLTYPEID type = 0;
            control.element->get_CurrentControlType(&type);
            if (action == zoomAudio::Action::OpenMenu && type == UIA_MenuItemControlTypeId)
                continue; // Audio Settings inside the menu is not its opener.
            if (match)
            {
                contextError = "More than one Zoom audio control matched. "
                               "The sound result is unverified; inspect the meeting in Live Diagnostics.";
                return ComPtr<IUIAutomationElement>();
            }
            match = control.element;
        }
        return match;
    };

    const zoomAudio::Backend backend{
        [&]
        {
            QStringList names;
            for (const Control& control : readControls())
                names.append(control.name);
            return zoomAudio::observe(names);
        },
        [&](zoomAudio::Action action)
        {
            const auto controls = readControls();
            QStringList names;
            for (const Control& control : controls)
                names.append(control.name);
            const auto observation = zoomAudio::observe(names);
            // State may have changed since the previous read. Never activate a
            // connection control on contradictory or newly unknown evidence.
            if ((action == zoomAudio::Action::LeaveAudio
                 && observation.connection != zoomAudio::Connection::Connected)
                || ((action == zoomAudio::Action::JoinAudio
                     || action == zoomAudio::Action::JoinComputerAudio)
                    && observation.connection != zoomAudio::Connection::Disconnected))
                return false;

            const auto element = uniqueControl(controls, action);
            if (!element || !targetSelected())
                return false;
            bool invoked = false;
            if (action == zoomAudio::Action::OpenMenu)
            {
                ComPtr<IUIAutomationExpandCollapsePattern> expand;
                if (SUCCEEDED(element->GetCurrentPatternAs(
                        UIA_ExpandCollapsePatternId, IID_PPV_ARGS(expand.GetAddressOf()))) && expand)
                {
                    ExpandCollapseState state = ExpandCollapseState_LeafNode;
                    if (FAILED(expand->get_CurrentExpandCollapseState(&state)))
                        return false;
                    if (state == ExpandCollapseState_Expanded)
                    {
                        searchPopups = true;
                        return true;
                    }
                    invoked = SUCCEEDED(expand->Expand());
                }
                else
                    invoked = invokeElement(element.Get());
                openedMenu = invoked;
                if (invoked)
                {
                    menuRuntimeId = uia::readRuntimeId(element.Get());
                    menuName = uia::readStringProperty(element.Get(), UIA_NamePropertyId);
                }
            }
            else
                invoked = invokeElement(element.Get());
            searchPopups = searchPopups || invoked;
            return invoked;
        },
        [] { QThread::msleep(kVerifyIntervalMs); }
    };

    const zoomAudio::Result outcome = connected.has_value()
        ? zoomAudio::setConnected(backend, *connected, kVerifyAttempts)
        : zoomAudio::readConnection(backend, kVerifyAttempts);

    // Close a menu opened for discovery if it is still present. Never dismiss a
    // join dialog or issue a blind Escape/click on an unrelated page.
    if (openedMenu && targetSelected())
    {
        const auto controls = readControls();
        bool leaveMenuVisible = false;
        for (const Control& control : controls)
        {
            if (zoomAudio::actionForName(control.name) == zoomAudio::Action::LeaveAudio)
                leaveMenuVisible = true;
        }
        ComPtr<IUIAutomationElement> opener;
        for (const Control& control : controls)
        {
            if (!menuRuntimeId.isEmpty() && uia::readRuntimeId(control.element.Get()) == menuRuntimeId)
            {
                opener = control.element;
                break;
            }
        }
        if (!opener && !menuName.isEmpty())
        {
            for (const Control& control : controls)
            {
                if (control.name == menuName)
                {
                    if (opener)
                    {
                        opener.Reset();
                        break;
                    }
                    opener = control.element;
                }
            }
        }
        if (opener && targetSelected())
        {
            ComPtr<IUIAutomationExpandCollapsePattern> expand;
            if (SUCCEEDED(opener->GetCurrentPatternAs(
                    UIA_ExpandCollapsePatternId, IID_PPV_ARGS(expand.GetAddressOf()))) && expand)
            {
                ExpandCollapseState state = ExpandCollapseState_LeafNode;
                if (SUCCEEDED(expand->get_CurrentExpandCollapseState(&state))
                    && state == ExpandCollapseState_Expanded)
                    expand->Collapse();
            }
            else if (leaveMenuVisible)
                invokeElement(opener.Get());
        }
    }

    const bool ok = outcome.ok && targetSelected() && contextError.isEmpty();
    QJsonObject result{
        {"ok", ok}, {"verified", ok}, {"changed", ok && outcome.changed},
        {"stateChangeAttempted", outcome.actionAttempted},
        {"audioState", ok ? zoomAudio::connectionName(outcome.observation.connection) : "unknown"},
        {"audioControlName", outcome.observation.controlName},
        {"state", ok ? outcome.observation.microphoneState : "unknown"},
        {"controlName", outcome.observation.microphoneControlName},
        {"tabRuntimeId", selectedId}
    };
    if (!ok)
        result.insert("error", contextError.isEmpty() ? outcome.error : contextError);
    return result;
}

QJsonObject ZoomMeetingController::readAudioConnection(
    quintptr hwndValue, const QString& tabRuntimeId, const QString& tabName) const
{
    return controlAudioConnection(hwndValue, tabRuntimeId, tabName, std::nullopt);
}

QJsonObject ZoomMeetingController::setAudioConnected(
    quintptr hwndValue, const QString& tabRuntimeId, const QString& tabName, bool connected) const
{
    return controlAudioConnection(hwndValue, tabRuntimeId, tabName, connected);
}

QJsonObject ZoomMeetingController::readMicrophone(
    quintptr hwndValue,
    const QString& tabRuntimeId,
    const QString& tabName) const
{
    // Chrome only renders the selected tab, so the meeting has to be on screen
    // in its own window before its controls can be read.
    const QJsonObject activation =
        activateTab(hwndValue, tabRuntimeId, tabName, false);

    if (!activation.value("ok").toBool())
        return activation;

    QJsonObject result = readMicrophoneInWindow(reinterpret_cast<HWND>(hwndValue));
    if (result.value("ok").toBool())
        result.insert("tabRuntimeId", activation.value("tabRuntimeId"));

    return result;
}

QJsonObject ZoomMeetingController::setMicrophoneMuted(
    quintptr hwndValue,
    const QString& tabRuntimeId,
    const QString& tabName,
    bool muted) const
{
    const QJsonObject activation =
        activateTab(hwndValue, tabRuntimeId, tabName, false);

    if (!activation.value("ok").toBool())
        return activation;

    HWND hwnd = reinterpret_cast<HWND>(hwndValue);
    const QString wanted = muted ? "muted" : "unmuted";

    const QJsonObject before = readMicrophoneInWindow(hwnd);
    if (!before.value("ok").toBool())
        return before;

    const QString current = before.value("state").toString();

    if (current == "audioNotConnected")
    {
        return failure(
            "That meeting has not joined audio yet, so its microphone cannot be "
            "changed. Join audio in Zoom first.");
    }

    if (current == "unknown")
    {
        return failure(
            "The microphone control was found but its state could not be read, so "
            "nothing was changed.");
    }

    if (current == wanted)
    {
        QJsonObject result = before;
        result.insert("changed", false);
        result.insert("verified", true);
        result.insert("tabRuntimeId", activation.value("tabRuntimeId"));
        return result;
    }

    ComPtr<IUIAutomationElement> document = findLiveDocument(hwnd);
    ComPtr<IUIAutomationElement> button =
        document ? findMicrophoneButton(document.Get()) : nullptr;

    if (!button || !invokeElement(button.Get()))
    {
        return failure(
            "The microphone control does not support being activated through UI "
            "Automation, so nothing was changed.");
    }

    for (int attempt = 0; attempt < kVerifyAttempts; ++attempt)
    {
        QThread::msleep(kVerifyIntervalMs);

        const QJsonObject after = readMicrophoneInWindow(hwnd);
        if (!after.value("ok").toBool())
            continue;

        if (after.value("state").toString() == wanted)
        {
            QJsonObject result = after;
            result.insert("changed", true);
            result.insert("verified", true);
            result.insert("tabRuntimeId", activation.value("tabRuntimeId"));
            return result;
        }
    }

    const QJsonObject unverified = readMicrophoneInWindow(hwnd);

    QJsonObject result = failure(
        "The microphone control was activated but Zoom did not report the new "
        "state, so the result is unverified.");
    result.insert("state", unverified.value("ok").toBool()
        ? unverified.value("state")
        : QJsonValue("unknown"));
    result.insert("verified", false);
    return result;
}
