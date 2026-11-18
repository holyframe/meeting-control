#include "CdpPipe.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace
{
std::wstring quoteWindowsArgument(const std::wstring& argument)
{
    std::wstring result = L"\"";
    std::size_t backslashes = 0;

    for (wchar_t ch : argument)
    {
        if (ch == L'\\')
        {
            ++backslashes;
            continue;
        }

        if (ch == L'"')
        {
            result.append(backslashes * 2 + 1, L'\\');
            result.push_back(L'"');
            backslashes = 0;
            continue;
        }

        result.append(backslashes, L'\\');
        backslashes = 0;
        result.push_back(ch);
    }

    result.append(backslashes * 2, L'\\');
    result.push_back(L'"');
    return result;
}

QString windowsError(const QString& operation, DWORD errorCode)
{
    return QString("%1 failed (Win32 error %2).")
        .arg(operation)
        .arg(static_cast<qulonglong>(errorCode));
}

void closeHandleIfValid(HANDLE& handle)
{
    if (handle && handle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(handle);
        handle = nullptr;
    }
}
}

CdpPipe::CdpPipe(QObject* parent)
    : QObject(parent)
{
}

CdpPipe::~CdpPipe()
{
    stop(true);
}

bool CdpPipe::start(const QString& chromeExecutable,
                    const QString& userDataDirectory,
                    QString* errorMessage)
{
    if (isProcessRunning())
    {
        if (errorMessage)
            *errorMessage = "A managed Chrome process is already running.";
        return false;
    }

    const QFileInfo chromeInfo(chromeExecutable);
    if (!chromeInfo.exists() || !chromeInfo.isFile())
    {
        if (errorMessage)
            *errorMessage = "Chrome executable was not found.";
        return false;
    }

    if (!QDir().mkpath(userDataDirectory))
    {
        if (errorMessage)
            *errorMessage = "Could not create the dedicated Chrome profile directory.";
        return false;
    }

    SECURITY_ATTRIBUTES securityAttributes{};
    securityAttributes.nLength = sizeof(SECURITY_ATTRIBUTES);
    securityAttributes.bInheritHandle = TRUE;

    HANDLE childRead = nullptr;
    HANDLE parentWrite = nullptr;
    HANDLE parentRead = nullptr;
    HANDLE childWrite = nullptr;

    // Pipe 1: parent writes commands; Chrome reads them.
    if (!CreatePipe(&childRead, &parentWrite, &securityAttributes, 0))
    {
        if (errorMessage)
            *errorMessage = windowsError("CreatePipe(input)", GetLastError());
        return false;
    }

    // Pipe 2: Chrome writes responses/events; parent reads them.
    if (!CreatePipe(&parentRead, &childWrite, &securityAttributes, 0))
    {
        const DWORD code = GetLastError();
        closeHandleIfValid(childRead);
        closeHandleIfValid(parentWrite);
        if (errorMessage)
            *errorMessage = windowsError("CreatePipe(output)", code);
        return false;
    }

    // Parent-side handles must not be inherited by Chrome.
    if (!SetHandleInformation(parentWrite, HANDLE_FLAG_INHERIT, 0) ||
        !SetHandleInformation(parentRead, HANDLE_FLAG_INHERIT, 0))
    {
        const DWORD code = GetLastError();
        closeHandleIfValid(childRead);
        closeHandleIfValid(parentWrite);
        closeHandleIfValid(parentRead);
        closeHandleIfValid(childWrite);
        if (errorMessage)
            *errorMessage = windowsError("SetHandleInformation", code);
        return false;
    }

    SIZE_T attributeListSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeListSize);

    std::vector<unsigned char> attributeStorage(attributeListSize);
    auto* attributeList =
        reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(attributeStorage.data());

    if (!InitializeProcThreadAttributeList(attributeList, 1, 0, &attributeListSize))
    {
        const DWORD code = GetLastError();
        closeHandleIfValid(childRead);
        closeHandleIfValid(parentWrite);
        closeHandleIfValid(parentRead);
        closeHandleIfValid(childWrite);
        if (errorMessage)
            *errorMessage = windowsError("InitializeProcThreadAttributeList", code);
        return false;
    }

    HANDLE inheritedHandles[2] = { childRead, childWrite };

    if (!UpdateProcThreadAttribute(
            attributeList,
            0,
            PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
            inheritedHandles,
            sizeof(inheritedHandles),
            nullptr,
            nullptr))
    {
        const DWORD code = GetLastError();
        DeleteProcThreadAttributeList(attributeList);
        closeHandleIfValid(childRead);
        closeHandleIfValid(parentWrite);
        closeHandleIfValid(parentRead);
        closeHandleIfValid(childWrite);
        if (errorMessage)
            *errorMessage = windowsError("UpdateProcThreadAttribute", code);
        return false;
    }

    const std::wstring executable = chromeInfo.absoluteFilePath().toStdWString();
    const std::wstring profile = QDir::toNativeSeparators(userDataDirectory).toStdWString();

    const std::wstring inputHandle = std::to_wstring(
        static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(childRead)));
    const std::wstring outputHandle = std::to_wstring(
        static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(childWrite)));

    std::wstring commandLine;
    commandLine += quoteWindowsArgument(executable);
    commandLine += L" " + quoteWindowsArgument(L"--user-data-dir=" + profile);
    commandLine += L" " + quoteWindowsArgument(L"--remote-debugging-pipe=asciiz");
    commandLine += L" " + quoteWindowsArgument(
        L"--remote-debugging-io-pipes=" + inputHandle + L"," + outputHandle);
    commandLine += L" " + quoteWindowsArgument(L"--no-first-run");
    commandLine += L" " + quoteWindowsArgument(L"--no-default-browser-check");
    commandLine += L" " + quoteWindowsArgument(L"about:blank");

    std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
    mutableCommandLine.push_back(L'\0');

    STARTUPINFOEXW startupInfo{};
    startupInfo.StartupInfo.cb = sizeof(STARTUPINFOEXW);
    startupInfo.lpAttributeList = attributeList;

    PROCESS_INFORMATION processInfo{};

    const BOOL created = CreateProcessW(
        executable.c_str(),
        mutableCommandLine.data(),
        nullptr,
        nullptr,
        TRUE,
        EXTENDED_STARTUPINFO_PRESENT,
        nullptr,
        nullptr,
        &startupInfo.StartupInfo,
        &processInfo);

    const DWORD createError = created ? ERROR_SUCCESS : GetLastError();

    DeleteProcThreadAttributeList(attributeList);

    // The child owns these inherited handles now.
    closeHandleIfValid(childRead);
    closeHandleIfValid(childWrite);

    if (!created)
    {
        closeHandleIfValid(parentRead);
        closeHandleIfValid(parentWrite);
        if (errorMessage)
            *errorMessage = windowsError("CreateProcessW", createError);
        return false;
    }

    CloseHandle(processInfo.hThread);

    parentRead_ = parentRead;
    parentWrite_ = parentWrite;
    processHandle_ = processInfo.hProcess;
    stopping_.store(false);

    readerThread_ = std::thread(&CdpPipe::readLoop, this);
    return true;
}

bool CdpPipe::sendMessage(const QByteArray& jsonMessage, QString* errorMessage)
{
    std::lock_guard<std::mutex> lock(writeMutex_);

    if (!parentWrite_ || parentWrite_ == INVALID_HANDLE_VALUE)
    {
        if (errorMessage)
            *errorMessage = "CDP input pipe is not available.";
        return false;
    }

    QByteArray framed = jsonMessage;
    framed.append('\0');

    qsizetype offset = 0;
    while (offset < framed.size())
    {
        DWORD written = 0;
        const DWORD remaining = static_cast<DWORD>(
            std::min<qsizetype>(framed.size() - offset, MAXDWORD));

        if (!WriteFile(parentWrite_,
                       framed.constData() + offset,
                       remaining,
                       &written,
                       nullptr))
        {
            if (errorMessage)
                *errorMessage = windowsError("WriteFile(CDP)", GetLastError());
            return false;
        }

        if (written == 0)
        {
            if (errorMessage)
                *errorMessage = "CDP pipe accepted zero bytes.";
            return false;
        }

        offset += written;
    }

    return true;
}

bool CdpPipe::isProcessRunning() const
{
    if (!processHandle_)
        return false;

    return WaitForSingleObject(processHandle_, 0) == WAIT_TIMEOUT;
}

void CdpPipe::readLoop()
{
    QByteArray currentMessage;
    char buffer[8192];

    while (!stopping_.load())
    {
        DWORD bytesRead = 0;
        const BOOL ok = ReadFile(
            parentRead_,
            buffer,
            static_cast<DWORD>(sizeof(buffer)),
            &bytesRead,
            nullptr);

        if (!ok || bytesRead == 0)
            break;

        for (DWORD i = 0; i < bytesRead; ++i)
        {
            const char ch = buffer[i];

            if (ch == '\0')
            {
                if (!currentMessage.isEmpty())
                {
                    emit messageReceived(currentMessage);
                    currentMessage.clear();
                }
                continue;
            }

            currentMessage.append(ch);

            // Guard against a corrupted or unexpectedly huge protocol frame.
            if (currentMessage.size() > 32 * 1024 * 1024)
            {
                emit pipeError("CDP message exceeded the 32 MiB safety limit.");
                currentMessage.clear();
            }
        }
    }

    if (!stopping_.load())
        emit pipeClosed();
}

void CdpPipe::stop(bool terminateBrowser)
{
    stopping_.store(true);

    {
        std::lock_guard<std::mutex> lock(writeMutex_);
        closeHandleIfValid(parentWrite_);
    }

    if (readerThread_.joinable())
    {
        CancelSynchronousIo(readerThread_.native_handle());
        readerThread_.join();
    }

    closeHandleIfValid(parentRead_);

    if (processHandle_)
    {
        if (terminateBrowser &&
            WaitForSingleObject(processHandle_, 2500) == WAIT_TIMEOUT)
        {
            TerminateProcess(processHandle_, 1);
            WaitForSingleObject(processHandle_, 1500);
        }

        closeHandleIfValid(processHandle_);
    }
}
