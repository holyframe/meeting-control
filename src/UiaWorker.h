#pragma once

#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <windows.h>
#include <objbase.h>
#include <UIAutomation.h>
#include <wrl/client.h>

class UiaWorker final : public QObject
{
    Q_OBJECT

public:
    explicit UiaWorker(QObject* parent = nullptr);
    ~UiaWorker() override;

public slots:
    void initialize();
    void scanChromeWindows();
    void inspectWindow(quintptr hwndValue);
    void shutdown();

signals:
    void initialized(bool success, const QString& message);
    void chromeWindowsFound(const QJsonArray& windows);
    void inspectionFinished(const QJsonObject& result);
    void diagnostic(const QString& message);

private:
    bool comInitialized_ = false;
    Microsoft::WRL::ComPtr<IUIAutomation> automation_;
    Microsoft::WRL::ComPtr<IUIAutomation2> automation2_;
};
