#pragma once

#include <QJsonObject>
#include <QString>

#include <windows.h>
#include <objbase.h>
#include <UIAutomation.h>
#include <wrl/client.h>

// Reads and changes the microphone of a Zoom meeting running in a Chrome tab.
// Every change reads the control first, acts only when the state differs, and
// reads it again afterwards: a result is reported as changed only once Zoom has
// confirmed the new state.
class ZoomMeetingController
{
public:
    explicit ZoomMeetingController(IUIAutomation* automation);

    QJsonObject activateTab(quintptr hwndValue,
                            const QString& tabRuntimeId,
                            const QString& tabName,
                            bool bringToFront) const;

    QJsonObject readMicrophone(quintptr hwndValue,
                               const QString& tabRuntimeId,
                               const QString& tabName) const;

    QJsonObject setMicrophoneMuted(quintptr hwndValue,
                                   const QString& tabRuntimeId,
                                   const QString& tabName,
                                   bool muted) const;

private:
    Microsoft::WRL::ComPtr<IUIAutomationElement> rootOf(HWND hwnd) const;

    Microsoft::WRL::ComPtr<IUIAutomationElement> findTab(
        const Microsoft::WRL::ComPtr<IUIAutomationElement>& root,
        const QString& tabRuntimeId,
        const QString& tabName) const;

    Microsoft::WRL::ComPtr<IUIAutomationElement> findLiveDocument(HWND hwnd) const;

    Microsoft::WRL::ComPtr<IUIAutomationElement> findMicrophoneButton(
        IUIAutomationElement* document) const;

    QJsonObject readMicrophoneInWindow(HWND hwnd) const;

    Microsoft::WRL::ComPtr<IUIAutomation> automation_;
};
