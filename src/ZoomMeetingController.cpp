#include "ZoomMeetingController.h"

#include "UiaSupport.h"

#include <QThread>

#include <OleAuto.h>

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

    if (ui.tabItems.empty())
        return root;

    for (const auto& item : ui.tabItems)
    {
        if (uia::readRuntimeId(item.Get()) == tabRuntimeId)
            return item;
    }

    // Chrome rebuilds tab elements when tabs are dragged or reordered, so the
    // runtime ID can go stale while the tab is still open.
    for (const auto& item : ui.tabItems)
    {
        if (uia::readStringProperty(item.Get(), UIA_NamePropertyId) == tabName)
            return item;
    }

    return {};
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

    bool selected = true;

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
