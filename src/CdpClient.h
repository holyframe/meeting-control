#pragma once

#include "CdpPipe.h"

#include <QObject>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <functional>

class CdpClient final : public QObject
{
    Q_OBJECT

public:
    using ResponseCallback = std::function<void(const QJsonObject&)>;

    explicit CdpClient(QObject* parent = nullptr);
    ~CdpClient() override;

    bool start(const QString& chromeExecutable,
               const QString& userDataDirectory,
               QString* errorMessage);

    bool isRunning() const;
    void refreshTargets(std::function<void(const QJsonArray&)> callback);
    void inspectTargetButtons(const QString& targetId,
                              std::function<void(const QJsonObject&)> callback);
    void shutdownBrowser();

signals:
    void errorOccurred(const QString& message);
    void eventReceived(const QJsonObject& event);

private slots:
    void onPipeMessage(const QByteArray& message);
    void onPipeError(const QString& message);
    void onPipeClosed();

private:
    void sendCommand(const QString& method,
                     const QJsonObject& params,
                     const QString& sessionId,
                     ResponseCallback callback);

    CdpPipe pipe_;
    qint64 nextCommandId_ = 1;
    QHash<qint64, ResponseCallback> pendingCallbacks_;
};
