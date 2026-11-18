#include "ChromeWindowEnumerator.h"

#include <QFileInfo>
#include <QJsonObject>

#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace
{
struct EnumContext
{
    QJsonArray windows;
};

QString readWindowTitle(HWND hwnd)
{
    const int length = GetWindowTextLengthW(hwnd);
    if (length <= 0)
        return {};

    std::vector<wchar_t> buffer(static_cast<std::size_t>(length) + 1);

    const int copied = GetWindowTextW(
        hwnd,
        buffer.data(),
        static_cast<int>(buffer.size()));

    if (copied <= 0)
        return {};

    return QString::fromWCharArray(buffer.data(), copied);
}

QString readWindowClass(HWND hwnd)
{
    wchar_t className[256]{};
    const int copied = GetClassNameW(hwnd, className, 255);

    if (copied <= 0)
        return {};

    return QString::fromWCharArray(className, copied);
}

QString readProcessImagePath(DWORD processId)
{
    HANDLE process = OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION,
        FALSE,
        processId);

    if (!process)
        return {};

    wchar_t path[32768]{};
    DWORD size = static_cast<DWORD>(sizeof(path) / sizeof(path[0]));

    const BOOL ok = QueryFullProcessImageNameW(
        process,
        0,
        path,
        &size);

    CloseHandle(process);

    if (!ok)
        return {};

    return QString::fromWCharArray(path, static_cast<qsizetype>(size));
}

BOOL CALLBACK enumWindowsCallback(HWND hwnd, LPARAM parameter)
{
    auto* context = reinterpret_cast<EnumContext*>(parameter);

    if (!IsWindowVisible(hwnd))
        return TRUE;

    DWORD processId = 0;
    GetWindowThreadProcessId(hwnd, &processId);

    if (processId == 0)
        return TRUE;

    const QString imagePath = readProcessImagePath(processId);
    if (imagePath.isEmpty())
        return TRUE;

    const QString executableName = QFileInfo(imagePath).fileName();
    if (executableName.compare("chrome.exe", Qt::CaseInsensitive) != 0)
        return TRUE;

    QJsonObject item;
    item.insert("title", readWindowTitle(hwnd));
    item.insert("processId", static_cast<qint64>(processId));
    item.insert("hwnd", ChromeWindowEnumerator::windowHandleToString(
        reinterpret_cast<quintptr>(hwnd)));
    item.insert("className", readWindowClass(hwnd));
    item.insert("imagePath", imagePath);

    context->windows.append(item);
    return TRUE;
}
}

QJsonArray ChromeWindowEnumerator::enumerateVisibleChromeWindows()
{
    EnumContext context;
    EnumWindows(enumWindowsCallback, reinterpret_cast<LPARAM>(&context));
    return context.windows;
}

QString ChromeWindowEnumerator::windowHandleToString(quintptr hwnd)
{
    return QString::number(static_cast<qulonglong>(hwnd), 16).toUpper();
}
