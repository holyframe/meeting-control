#pragma once

#include <QObject>
#include <QByteArray>
#include <QString>

#include <atomic>
#include <mutex>
#include <thread>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

class CdpPipe final : public QObject
{
    Q_OBJECT

public:
    explicit CdpPipe(QObject* parent = nullptr);
    ~CdpPipe() override;

    bool start(const QString& chromeExecutable,
               const QString& userDataDirectory,
               QString* errorMessage);

    bool sendMessage(const QByteArray& jsonMessage, QString* errorMessage = nullptr);
    bool isProcessRunning() const;
    void stop(bool terminateBrowser);

signals:
    void messageReceived(const QByteArray& message);
    void pipeError(const QString& message);
    void pipeClosed();

private:
    void readLoop();

    HANDLE parentRead_ = INVALID_HANDLE_VALUE;
    HANDLE parentWrite_ = INVALID_HANDLE_VALUE;
    HANDLE processHandle_ = nullptr;

    std::thread readerThread_;
    std::atomic_bool stopping_{false};
    mutable std::mutex writeMutex_;
};
