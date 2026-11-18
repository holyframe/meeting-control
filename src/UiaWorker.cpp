#include "UiaWorker.h"

#include "ChromeWindowEnumerator.h"
#include "UiaTreeInspector.h"

#include <QJsonObject>

#include <objbase.h>
#include <UIAutomation.h>

using Microsoft::WRL::ComPtr;

UiaWorker::UiaWorker(QObject* parent)
    : QObject(parent)
{
}

UiaWorker::~UiaWorker()
{
    automation2_.Reset();
    automation_.Reset();

    if (comInitialized_)
    {
        CoUninitialize();
        comInitialized_ = false;
    }
}

void UiaWorker::initialize()
{
    const HRESULT comHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    if (FAILED(comHr))
    {
        emit initialized(false,
            QString("CoInitializeEx failed: 0x%1")
                .arg(static_cast<quint32>(comHr), 8, 16, QLatin1Char('0')));
        return;
    }

    comInitialized_ = true;

    const HRESULT createHr = CoCreateInstance(
        CLSID_CUIAutomation,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(automation_.GetAddressOf()));

    if (FAILED(createHr) || !automation_)
    {
        emit initialized(false,
            QString("Could not create CUIAutomation: 0x%1")
                .arg(static_cast<quint32>(createHr), 8, 16, QLatin1Char('0')));
        return;
    }

    if (SUCCEEDED(automation_.As(&automation2_)) && automation2_)
    {
        // Bound provider wait time so a stale/hung Chrome window is less likely
        // to stall the controller for the full default timeout.
        automation2_->put_ConnectionTimeout(2000);
        automation2_->put_TransactionTimeout(3000);
    }

    emit initialized(true, "Windows UI Automation initialized.");
}

void UiaWorker::scanChromeWindows()
{
    const QJsonArray windows =
        ChromeWindowEnumerator::enumerateVisibleChromeWindows();

    emit chromeWindowsFound(windows);
}

void UiaWorker::inspectWindow(quintptr hwndValue)
{
    if (!automation_)
    {
        emit inspectionFinished(QJsonObject{
            {"ok", false},
            {"error", "UI Automation has not initialized."}
        });
        return;
    }

    HWND hwnd = reinterpret_cast<HWND>(hwndValue);

    UiaTreeInspector inspector(automation_.Get());
    const QJsonObject result = inspector.inspectWindow(hwnd);

    emit inspectionFinished(result);
}

void UiaWorker::shutdown()
{
    automation2_.Reset();
    automation_.Reset();

    if (comInitialized_)
    {
        CoUninitialize();
        comInitialized_ = false;
    }
}
