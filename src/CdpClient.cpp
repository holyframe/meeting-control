#include "CdpClient.h"
#include "ZoomInspector.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QThread>

CdpClient::CdpClient(QObject* parent)
    : QObject(parent)
{
    connect(&pipe_, &CdpPipe::messageReceived,
            this, &CdpClient::onPipeMessage, Qt::QueuedConnection);
    connect(&pipe_, &CdpPipe::pipeError,
            this, &CdpClient::onPipeError, Qt::QueuedConnection);
    connect(&pipe_, &CdpPipe::pipeClosed,
            this, &CdpClient::onPipeClosed, Qt::QueuedConnection);
}

CdpClient::~CdpClient()
{
    shutdownBrowser();
}

bool CdpClient::start(const QString& chromeExecutable,
                      const QString& userDataDirectory,
                      QString* errorMessage)
{
    return pipe_.start(chromeExecutable, userDataDirectory, errorMessage);
}

bool CdpClient::isRunning() const
{
    return pipe_.isProcessRunning();
}

void CdpClient::sendCommand(const QString& method,
                            const QJsonObject& params,
                            const QString& sessionId,
                            ResponseCallback callback)
{
    const qint64 id = nextCommandId_++;

    QJsonObject message;
    message.insert("id", id);
    message.insert("method", method);
    message.insert("params", params);

    if (!sessionId.isEmpty())
        message.insert("sessionId", sessionId);

    if (callback)
        pendingCallbacks_.insert(id, std::move(callback));

    const QByteArray json = QJsonDocument(message).toJson(QJsonDocument::Compact);
    QString error;

    if (!pipe_.sendMessage(json, &error))
    {
        pendingCallbacks_.remove(id);
        emit errorOccurred(error);
    }
}

void CdpClient::refreshTargets(std::function<void(const QJsonArray&)> callback)
{
    sendCommand("Target.getTargets", QJsonObject(), QString(),
        [callback = std::move(callback)](const QJsonObject& response)
        {
            const QJsonArray targets =
                response.value("result").toObject()
                        .value("targetInfos").toArray();

            if (callback)
                callback(targets);
        });
}

void CdpClient::inspectTargetButtons(
    const QString& targetId,
    std::function<void(const QJsonObject&)> callback)
{
    QJsonObject attachParams;
    attachParams.insert("targetId", targetId);
    attachParams.insert("flatten", true);

    sendCommand("Target.attachToTarget", attachParams, QString(),
        [this, callback = std::move(callback)](const QJsonObject& attachResponse)
        {
            if (attachResponse.contains("error"))
            {
                if (callback)
                    callback(QJsonObject{
                        {"ok", false},
                        {"error", attachResponse.value("error").toObject()}
                    });
                return;
            }

            const QString sessionId =
                attachResponse.value("result").toObject()
                              .value("sessionId").toString();

            if (sessionId.isEmpty())
            {
                if (callback)
                    callback(QJsonObject{
                        {"ok", false},
                        {"error", "Chrome did not return a CDP session ID."}
                    });
                return;
            }

            QJsonObject evaluateParams;
            evaluateParams.insert("expression", ZoomInspector::buttonInventoryExpression());
            evaluateParams.insert("returnByValue", true);
            evaluateParams.insert("awaitPromise", true);

            sendCommand("Runtime.evaluate", evaluateParams, sessionId,
                [this, sessionId, callback = std::move(callback)]
                (const QJsonObject& evaluateResponse)
                {
                    QJsonObject inspectionResult;

                    if (evaluateResponse.contains("error"))
                    {
                        inspectionResult = QJsonObject{
                            {"ok", false},
                            {"error", evaluateResponse.value("error").toObject()}
                        };
                    }
                    else
                    {
                        const QJsonObject result =
                            evaluateResponse.value("result").toObject();

                        if (result.contains("exceptionDetails"))
                        {
                            inspectionResult = QJsonObject{
                                {"ok", false},
                                {"exceptionDetails", result.value("exceptionDetails")}
                            };
                        }
                        else
                        {
                            const QJsonObject remoteObject =
                                result.value("result").toObject();

                            inspectionResult = QJsonObject{
                                {"ok", true},
                                {"value", remoteObject.value("value")},
                                {"description", remoteObject.value("description")}
                            };
                        }
                    }

                    // Release the temporary flattened target session after inspection.
                    sendCommand(
                        "Target.detachFromTarget",
                        QJsonObject{{"sessionId", sessionId}},
                        QString(),
                        {});

                    if (callback)
                        callback(inspectionResult);
                });
        });
}

void CdpClient::shutdownBrowser()
{
    if (!pipe_.isProcessRunning())
    {
        pipe_.stop(true);
        return;
    }

    // Ask Chrome to close normally first. The pipe is closed and the process
    // is force-terminated only if it does not exit within the grace period.
    sendCommand("Browser.close", QJsonObject(), QString(), {});
    QThread::msleep(900);
    pipe_.stop(true);
}

void CdpClient::onPipeMessage(const QByteArray& message)
{
    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(message, &parseError);

    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        emit errorOccurred(
            QString("Could not parse CDP message: %1").arg(parseError.errorString()));
        return;
    }

    const QJsonObject object = document.object();

    if (object.contains("id"))
    {
        const qint64 id = object.value("id").toVariant().toLongLong();
        const auto callback = pendingCallbacks_.take(id);

        if (callback)
            callback(object);

        return;
    }

    emit eventReceived(object);
}

void CdpClient::onPipeError(const QString& message)
{
    emit errorOccurred(message);
}

void CdpClient::onPipeClosed()
{
    emit errorOccurred("Chrome's CDP pipe closed.");
}
