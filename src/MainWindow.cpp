#include "MainWindow.h"
#include "UiaWorker.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabWidget>
#include <QThread>
#include <QVBoxLayout>
#include <QWidget>
#include <QMetaObject>

namespace
{
QLabel* makeMutedLabel(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setObjectName("mutedText");
    label->setWordWrap(true);
    return label;
}

QFrame* makePanel(QWidget* parent, const QString& objectName = "panel")
{
    auto* panel = new QFrame(parent);
    panel->setObjectName(objectName);
    return panel;
}

QPushButton* makeNavButton(const QString& text, QWidget* parent, bool checkable)
{
    auto* button = new QPushButton(text, parent);
    button->setObjectName("sidebarNavButton");
    button->setCheckable(checkable);
    button->setCursor(Qt::PointingHandCursor);
    button->setMinimumHeight(44);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    button->setText("  " + text);
    return button;
}

QFrame* makeMetricCard(
    QWidget* parent,
    const QString& symbol,
    const QString& heading,
    const QString& value,
    const QString& detail,
    const QString& accent,
    QLabel** valueOutput,
    QLabel** detailOutput)
{
    auto* card = makePanel(parent, "metricCard");
    auto* row = new QHBoxLayout(card);
    row->setContentsMargins(16, 14, 16, 14);
    row->setSpacing(12);

    auto* iconTile = new QLabel(symbol, card);
    iconTile->setObjectName("metricIcon");
    iconTile->setAlignment(Qt::AlignCenter);
    iconTile->setFixedSize(48, 48);
    iconTile->setStyleSheet(QString(
        "QLabel#metricIcon { color:white; background:%1; border-radius:11px; "
        "font-size:20pt; font-weight:700; }").arg(accent));

    auto* textColumn = new QVBoxLayout();
    textColumn->setSpacing(3);

    auto* headingLabel = new QLabel(heading, card);
    headingLabel->setObjectName("metricHeading");

    auto* valueLabel = new QLabel(value, card);
    valueLabel->setObjectName("metricValue");

    auto* detailLabel = makeMutedLabel(detail, card);
    detailLabel->setObjectName("metricDetail");

    textColumn->addWidget(headingLabel);
    textColumn->addWidget(valueLabel);
    textColumn->addWidget(detailLabel);
    textColumn->addStretch();

    row->addWidget(iconTile, 0, Qt::AlignVCenter);
    row->addLayout(textColumn, 1);

    if (valueOutput)
        *valueOutput = valueLabel;
    if (detailOutput)
        *detailOutput = detailLabel;

    return card;
}

}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("Meeting Controller");
    resize(1500, 920);
    setMinimumSize(1120, 720);

    auto* root = new QWidget(this);
    root->setObjectName("appRoot");
    auto* shell = new QHBoxLayout(root);
    shell->setContentsMargins(0, 0, 0, 0);
    shell->setSpacing(0);

    shell->addWidget(createSidebar());

    pageStack_ = new QStackedWidget(root);
    pageStack_->setObjectName("pageStack");

    dashboardPage_ = createDashboardPage();
    diagnosticsPage_ = createDiagnosticsPage();
    settingsPage_ = createSettingsPage();
    aboutPage_ = createAboutPage();

    pageStack_->addWidget(dashboardPage_);
    pageStack_->addWidget(diagnosticsPage_);
    pageStack_->addWidget(settingsPage_);
    pageStack_->addWidget(aboutPage_);

    shell->addWidget(pageStack_, 1);
    setCentralWidget(root);

    connect(dashboardNavButton_, &QPushButton::clicked,
            this, [this] { navigateTo(0); });
    connect(diagnosticsNavButton_, &QPushButton::clicked,
            this, [this] { navigateTo(1); });
    connect(settingsNavButton_, &QPushButton::clicked,
            this, [this] { navigateTo(2); });
    connect(aboutNavButton_, &QPushButton::clicked,
            this, [this] { navigateTo(3); });

    connect(sidebarPickUpButton_, &QPushButton::clicked,
            this, &MainWindow::requestActiveTabPickup);
    connect(dashboardPickUpButton_, &QPushButton::clicked,
            this, &MainWindow::requestActiveTabPickup);

    connect(sidebarRefreshButton_, &QPushButton::clicked,
            this, &MainWindow::refreshChromeWindows);
    connect(dashboardRefreshButton_, &QPushButton::clicked,
            this, &MainWindow::refreshChromeWindows);

    connect(inspectButton_, &QPushButton::clicked,
            this, &MainWindow::inspectSelectedWindow);

    connect(chromeWindowCombo_, &QComboBox::currentIndexChanged,
            this, [this](int index)
    {
        Q_UNUSED(index);
        inspectButton_->setEnabled(
            uiaReady_ && !chromeWindowCombo_->currentData().toString().isEmpty());
    });

    workerThread_ = new QThread(this);
    worker_ = new UiaWorker();
    worker_->moveToThread(workerThread_);

    connect(workerThread_, &QThread::started,
            worker_, &UiaWorker::initialize);
    connect(this, &MainWindow::scanRequested,
            worker_, &UiaWorker::scanChromeWindows, Qt::QueuedConnection);
    connect(this, &MainWindow::inspectRequested,
            worker_, &UiaWorker::inspectWindow, Qt::QueuedConnection);
    connect(worker_, &UiaWorker::initialized,
            this, &MainWindow::onInitialized, Qt::QueuedConnection);
    connect(worker_, &UiaWorker::chromeWindowsFound,
            this, &MainWindow::onChromeWindowsFound, Qt::QueuedConnection);
    connect(worker_, &UiaWorker::inspectionFinished,
            this, &MainWindow::onInspectionFinished, Qt::QueuedConnection);
    connect(worker_, &UiaWorker::diagnostic,
            this, &MainWindow::appendLog, Qt::QueuedConnection);
    connect(workerThread_, &QThread::finished,
            worker_, &QObject::deleteLater);

    applyDarkProfessionalTheme();
    navigateTo(0);
    statusBar()->showMessage(
        "Starting Windows UI Automation · Microphone automation is not enabled");
    workerThread_->start();
}

MainWindow::~MainWindow()
{
    if (workerThread_ && workerThread_->isRunning())
    {
        if (worker_)
        {
            QMetaObject::invokeMethod(
                worker_, "shutdown", Qt::BlockingQueuedConnection);
        }

        workerThread_->quit();
        workerThread_->wait();
    }
}

QWidget* MainWindow::createSidebar()
{
    auto* sidebar = new QFrame(this);
    sidebar->setObjectName("sidebar");
    sidebar->setFixedWidth(286);

    auto* layout = new QVBoxLayout(sidebar);
    layout->setContentsMargins(18, 22, 18, 18);
    layout->setSpacing(8);

    auto* brandRow = new QHBoxLayout();
    brandRow->setSpacing(11);

    auto* logo = new QLabel("≋", sidebar);
    logo->setObjectName("brandLogo");
    logo->setAlignment(Qt::AlignCenter);
    logo->setFixedSize(48, 48);

    auto* brandText = new QVBoxLayout();
    brandText->setSpacing(1);
    auto* brandName = new QLabel("Meeting Controller", sidebar);
    brandName->setObjectName("brandName");
    auto* brandSubtitle = makeMutedLabel("Chrome · Zoom · UI Automation", sidebar);
    brandSubtitle->setObjectName("brandSubtitle");
    brandText->addWidget(brandName);
    brandText->addWidget(brandSubtitle);

    brandRow->addWidget(logo);
    brandRow->addLayout(brandText, 1);
    layout->addLayout(brandRow);
    layout->addSpacing(28);

    dashboardNavButton_ = makeNavButton("⌂   Dashboard", sidebar, true);
    sidebarPickUpButton_ = makeNavButton("＋   Pick Up Active Meeting Tab", sidebar, false);
    sidebarRefreshButton_ = makeNavButton("⟳   Refresh Chrome Windows", sidebar, false);
    sidebarMuteAllButton_ = makeNavButton("◉   Mute All Meetings", sidebar, false);

    layout->addWidget(dashboardNavButton_);
    layout->addWidget(sidebarPickUpButton_);
    layout->addWidget(sidebarRefreshButton_);
    layout->addWidget(sidebarMuteAllButton_);

    auto* separator = new QFrame(sidebar);
    separator->setObjectName("sidebarSeparator");
    separator->setFixedHeight(1);
    layout->addSpacing(10);
    layout->addWidget(separator);
    layout->addSpacing(10);

    diagnosticsNavButton_ = makeNavButton("⌁   Live Diagnostics", sidebar, true);
    settingsNavButton_ = makeNavButton("⚙   Settings", sidebar, true);
    aboutNavButton_ = makeNavButton("ⓘ   About", sidebar, true);

    layout->addWidget(diagnosticsNavButton_);
    layout->addWidget(settingsNavButton_);
    layout->addWidget(aboutNavButton_);
    layout->addStretch();

    auto* statusCard = makePanel(sidebar, "sidebarStatusCard");
    auto* statusLayout = new QVBoxLayout(statusCard);
    statusLayout->setContentsMargins(12, 12, 12, 12);
    statusLayout->setSpacing(4);

    auto* statusRow = new QHBoxLayout();
    auto* statusDot = new QLabel("●", statusCard);
    statusDot->setObjectName("statusDot");
    sidebarUiaState_ = new QLabel("UIA Initializing", statusCard);
    sidebarUiaState_->setObjectName("sidebarStatusTitle");
    statusRow->addWidget(statusDot);
    statusRow->addWidget(sidebarUiaState_);
    statusRow->addStretch();

    sidebarUiaDetail_ = makeMutedLabel(
        "Windows UI Automation is starting", statusCard);
    statusLayout->addLayout(statusRow);
    statusLayout->addWidget(sidebarUiaDetail_);
    layout->addWidget(statusCard);

    sidebarMuteAllButton_->setEnabled(false);
    sidebarMuteAllButton_->setToolTip(
        "Disabled until Zoom microphone controls can be identified and verified.");

    return sidebar;
}

QWidget* MainWindow::createDashboardPage()
{
    auto* page = new QWidget(this);
    page->setObjectName("pageRoot");

    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 22, 28, 20);
    layout->setSpacing(18);

    auto* header = new QWidget(page);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);

    auto* titleColumn = new QVBoxLayout();
    titleColumn->setSpacing(3);
    auto* title = new QLabel("Dashboard", header);
    title->setObjectName("pageTitle");
    auto* subtitle = makeMutedLabel(
        "Manage your Zoom meetings in Chrome", header);
    subtitle->setObjectName("pageSubtitle");
    titleColumn->addWidget(title);
    titleColumn->addWidget(subtitle);
    headerLayout->addLayout(titleColumn);
    headerLayout->addStretch();

    dashboardChromeSummary_ = makeMutedLabel(
        "0 Chrome windows detected", header);
    dashboardChromeSummary_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    headerLayout->addWidget(dashboardChromeSummary_);
    layout->addWidget(header);

    auto* metrics = new QGridLayout();
    metrics->setHorizontalSpacing(14);
    metrics->setVerticalSpacing(14);

    metrics->addWidget(makeMetricCard(
        page, "♟", "Total Meetings", "0", "No tabs picked up yet",
        "#1765C5", &totalMeetingsValue_, &totalMeetingsDetail_), 0, 0);

    metrics->addWidget(makeMetricCard(
        page, "▶", "Active Meeting", "None", "No active meeting selected",
        "#078A59", &activeMeetingValue_, &activeMeetingDetail_), 0, 1);

    metrics->addWidget(makeMetricCard(
        page, "◉", "Safety Status", "Not verified", "Microphone controls are locked",
        "#078A59", &safetyValue_, &safetyDetail_), 0, 2);

    metrics->addWidget(makeMetricCard(
        page, "▣", "UIA Connection", "Starting", "Windows UI Automation",
        "#1765C5", &uiaValue_, &uiaDetail_), 0, 3);

    for (int column = 0; column < 4; ++column)
        metrics->setColumnStretch(column, 1);
    layout->addLayout(metrics);

    auto* actionRow = new QHBoxLayout();
    actionRow->setSpacing(12);

    dashboardPickUpButton_ = new QPushButton("＋  Pick Up Active Meeting Tab", page);
    dashboardPickUpButton_->setObjectName("primaryButton");
    dashboardPickUpButton_->setMinimumHeight(50);

    dashboardRefreshButton_ = new QPushButton("⟳  Refresh Chrome Windows", page);
    dashboardRefreshButton_->setObjectName("secondaryButton");
    dashboardRefreshButton_->setMinimumHeight(50);

    dashboardMuteAllButton_ = new QPushButton("◉  Mute All Meetings", page);
    dashboardMuteAllButton_->setObjectName("dangerButton");
    dashboardMuteAllButton_->setMinimumHeight(50);
    dashboardMuteAllButton_->setEnabled(false);
    dashboardMuteAllButton_->setToolTip(
        "Microphone automation is disabled until the Zoom control and its resulting state can be verified.");

    releaseActiveButton_ = new QPushButton("▷  Release Active Meeting", page);
    releaseActiveButton_->setObjectName("secondaryButton");
    releaseActiveButton_->setMinimumHeight(50);
    releaseActiveButton_->setEnabled(false);
    releaseActiveButton_->setToolTip("No meeting is currently registered as active.");

    actionRow->addWidget(dashboardPickUpButton_, 5);
    actionRow->addWidget(dashboardRefreshButton_, 4);
    actionRow->addWidget(dashboardMuteAllButton_, 3);
    actionRow->addWidget(releaseActiveButton_, 4);
    layout->addLayout(actionRow);

    auto* meetingHeader = new QHBoxLayout();
    auto* meetingTitle = new QLabel("♟  Managed Meetings", page);
    meetingTitle->setObjectName("sectionTitle");
    meetingCountLabel_ = new QLabel("0", page);
    meetingCountLabel_->setObjectName("countPill");
    meetingHeader->addWidget(meetingTitle);
    meetingHeader->addWidget(meetingCountLabel_);
    meetingHeader->addStretch();
    layout->addLayout(meetingHeader);

    auto* meetingsArea = new QScrollArea(page);
    meetingsArea->setObjectName("meetingsScrollArea");
    meetingsArea->setWidgetResizable(true);
    meetingsArea->setFrameShape(QFrame::NoFrame);

    auto* meetingsContent = new QWidget(meetingsArea);
    auto* meetingsLayout = new QGridLayout(meetingsContent);
    meetingsLayout->setContentsMargins(0, 2, 0, 8);
    meetingsLayout->setSpacing(14);

    auto* emptyCard = makePanel(meetingsContent, "emptyMeetingsCard");
    auto* emptyLayout = new QVBoxLayout(emptyCard);
    emptyLayout->setContentsMargins(24, 34, 24, 34);
    emptyLayout->setSpacing(10);
    emptyLayout->setAlignment(Qt::AlignCenter);

    auto* emptyIcon = new QLabel("▣", emptyCard);
    emptyIcon->setObjectName("emptyIcon");
    emptyIcon->setAlignment(Qt::AlignCenter);
    auto* emptyTitle = new QLabel("No meetings picked up", emptyCard);
    emptyTitle->setObjectName("emptyTitle");
    auto* emptyDescription = makeMutedLabel(
        "Switch to a Zoom meeting tab in Chrome, then choose "
        "Pick Up Active Meeting Tab. Only the active Zoom tab will be considered.",
        emptyCard);
    emptyDescription->setAlignment(Qt::AlignCenter);
    emptyDescription->setMaximumWidth(520);

    emptyLayout->addWidget(emptyIcon);
    emptyLayout->addWidget(emptyTitle);
    emptyLayout->addWidget(emptyDescription);
    meetingsLayout->addWidget(emptyCard, 0, 0, 1, 3);

    meetingsArea->setWidget(meetingsContent);
    layout->addWidget(meetingsArea, 1);

    auto* footerHint = makeMutedLabel(
        "Meeting Controller does not change microphone state until the target Zoom control is identified and its state can be verified.",
        page);
    layout->addWidget(footerHint);

    return page;
}

QWidget* MainWindow::createDiagnosticsPage()
{
    auto* page = new QWidget(this);
    page->setObjectName("pageRoot");

    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 22, 28, 20);
    layout->setSpacing(16);

    auto* title = new QLabel("Live Diagnostics", page);
    title->setObjectName("pageTitle");
    auto* subtitle = makeMutedLabel(
        "Inspect existing Chrome windows and review their Windows UI Automation accessibility tree.",
        page);
    subtitle->setObjectName("pageSubtitle");
    layout->addWidget(title);
    layout->addWidget(subtitle);

    auto* browserPanel = makePanel(page, "panel");
    auto* browserLayout = new QVBoxLayout(browserPanel);
    browserLayout->setContentsMargins(16, 14, 16, 14);
    browserLayout->setSpacing(10);

    auto* browserTitle = new QLabel("CHROME WINDOW INSPECTOR", browserPanel);
    browserTitle->setObjectName("panelHeading");
    browserLayout->addWidget(browserTitle);

    auto* controls = new QHBoxLayout();
    auto* browserLabel = new QLabel("Chrome window", browserPanel);
    browserLabel->setObjectName("mutedText");

    chromeWindowCombo_ = new QComboBox(browserPanel);
    chromeWindowCombo_->setObjectName("chromeWindowCombo");
    chromeWindowCombo_->setMinimumWidth(340);
    chromeWindowCombo_->addItem("No Chrome windows detected", QString());

    auto* refreshButton = new QPushButton("⟳  Refresh", browserPanel);
    refreshButton->setObjectName("secondaryButton");
    refreshButton->setMinimumHeight(40);

    inspectButton_ = new QPushButton("Inspect Selected Window", browserPanel);
    inspectButton_->setObjectName("secondaryButton");
    inspectButton_->setMinimumHeight(40);
    inspectButton_->setEnabled(false);

    controls->addWidget(browserLabel);
    controls->addWidget(chromeWindowCombo_, 1);
    controls->addWidget(refreshButton);
    controls->addWidget(inspectButton_);
    browserLayout->addLayout(controls);
    layout->addWidget(browserPanel);

    diagnosticsTabs_ = new QTabWidget(page);
    diagnosticsTabs_->setObjectName("diagnosticsTabs");

    liveDiagnostics_ = new QPlainTextEdit(diagnosticsTabs_);
    liveDiagnostics_->setObjectName("diagnosticsText");
    liveDiagnostics_->setReadOnly(true);
    liveDiagnostics_->setPlaceholderText(
        "Timestamped UI Automation events, scans, warnings, and errors will appear here.");

    accessibilityTree_ = new QPlainTextEdit(diagnosticsTabs_);
    accessibilityTree_->setObjectName("diagnosticsText");
    accessibilityTree_->setReadOnly(true);
    accessibilityTree_->setPlaceholderText(
        "Inspect a Chrome window to view its UI Automation accessibility tree.");

    diagnosticsTabs_->addTab(liveDiagnostics_, "Live Activity");
    diagnosticsTabs_->addTab(accessibilityTree_, "Accessibility Tree");
    layout->addWidget(diagnosticsTabs_, 1);

    auto* note = makeMutedLabel(
        "Read-only diagnostics. This build does not click page controls, pick up meeting tabs, or mute/unmute Zoom.",
        page);
    layout->addWidget(note);

    connect(refreshButton, &QPushButton::clicked,
            this, &MainWindow::refreshChromeWindows);

    return page;
}

QWidget* MainWindow::createSettingsPage()
{
    auto* page = new QWidget(this);
    page->setObjectName("pageRoot");
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 22, 28, 20);
    layout->setSpacing(16);

    auto* title = new QLabel("Settings", page);
    title->setObjectName("pageTitle");
    auto* subtitle = makeMutedLabel(
        "Application behavior and safety configuration.", page);
    subtitle->setObjectName("pageSubtitle");
    layout->addWidget(title);
    layout->addWidget(subtitle);

    auto* safetyPanel = makePanel(page, "panel");
    auto* safetyLayout = new QVBoxLayout(safetyPanel);
    safetyLayout->setContentsMargins(18, 16, 18, 16);
    safetyLayout->setSpacing(8);
    auto* safetyHeading = new QLabel("MICROPHONE SAFETY", safetyPanel);
    safetyHeading->setObjectName("panelHeading");
    auto* safetyText = new QLabel(
        "Microphone automation is locked in this build. It will only be enabled after "
        "the Zoom microphone control can be located through UI Automation and the "
        "resulting mute state can be independently verified.", safetyPanel);
    safetyText->setObjectName("settingsBody");
    safetyText->setWordWrap(true);
    safetyLayout->addWidget(safetyHeading);
    safetyLayout->addWidget(safetyText);
    layout->addWidget(safetyPanel);

    auto* browserPanel = makePanel(page, "panel");
    auto* browserLayout = new QVBoxLayout(browserPanel);
    browserLayout->setContentsMargins(18, 16, 18, 16);
    browserLayout->setSpacing(8);
    auto* browserHeading = new QLabel("BROWSER INTEGRATION", browserPanel);
    browserHeading->setObjectName("panelHeading");
    auto* browserText = new QLabel(
        "Uses Windows UI Automation to inspect existing Chrome windows. No Chrome "
        "extension, remote-debugging port, dedicated Chrome profile, or browser "
        "launch changes are required.", browserPanel);
    browserText->setObjectName("settingsBody");
    browserText->setWordWrap(true);
    browserLayout->addWidget(browserHeading);
    browserLayout->addWidget(browserText);
    layout->addWidget(browserPanel);
    layout->addStretch();

    return page;
}

QWidget* MainWindow::createAboutPage()
{
    auto* page = new QWidget(this);
    page->setObjectName("pageRoot");
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 22, 28, 20);
    layout->setSpacing(16);

    auto* title = new QLabel("About Meeting Controller", page);
    title->setObjectName("pageTitle");
    auto* subtitle = makeMutedLabel(
        "A native Windows controller for managing existing Chrome Zoom sessions.",
        page);
    subtitle->setObjectName("pageSubtitle");
    layout->addWidget(title);
    layout->addWidget(subtitle);

    auto* panel = makePanel(page, "panel");
    auto* panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(20, 18, 20, 18);
    panelLayout->setSpacing(10);

    auto* appName = new QLabel("Meeting Controller", panel);
    appName->setObjectName("sectionTitle");
    auto* version = makeMutedLabel("Version 0.4.0 · Windows UIA feasibility build", panel);
    auto* technology = new QLabel(
        "C++20 · Qt 6 Widgets · Windows UI Automation · CMake", panel);
    technology->setObjectName("settingsBody");
    auto* scope = makeMutedLabel(
        "Current scope: Chrome window discovery and accessibility-tree inspection. "
        "Active-tab pickup and microphone control are not implemented yet.", panel);
    panelLayout->addWidget(appName);
    panelLayout->addWidget(version);
    panelLayout->addWidget(technology);
    panelLayout->addWidget(scope);

    layout->addWidget(panel);
    layout->addStretch();
    return page;
}

void MainWindow::navigateTo(int pageIndex)
{
    if (!pageStack_ || pageIndex < 0 || pageIndex >= pageStack_->count())
        return;

    pageStack_->setCurrentIndex(pageIndex);
    dashboardNavButton_->setChecked(pageIndex == 0);
    diagnosticsNavButton_->setChecked(pageIndex == 1);
    settingsNavButton_->setChecked(pageIndex == 2);
    aboutNavButton_->setChecked(pageIndex == 3);

    if (pageIndex == 1)
        statusBar()->showMessage("Live Diagnostics · Read-only UI Automation inspection");
    else if (pageIndex == 0)
        statusBar()->showMessage("Dashboard · Microphone automation disabled pending validation");
    else
        statusBar()->showMessage("Meeting Controller");
}

void MainWindow::onInitialized(bool success, const QString& message)
{
    uiaReady_ = success;
    updateUiaStatus(success, message);

    if (success)
        refreshChromeWindows();
    else
        appendLog(message);
}

void MainWindow::updateUiaStatus(bool ready, const QString& message)
{
    const QString state = ready ? "Ready" : "Unavailable";
    uiaValue_->setText(state);
    uiaDetail_->setText(ready
        ? "Windows UI Automation initialized"
        : "Initialization failed");

    sidebarUiaState_->setText(ready ? "UIA Ready" : "UIA Unavailable");
    sidebarUiaDetail_->setText(message);
    statusBar()->showMessage(message);

    if (ready)
    {
        sidebarUiaState_->setStyleSheet("color:#86EFAC; font-weight:700;");
    }
    else
    {
        sidebarUiaState_->setStyleSheet("color:#FCA5A5; font-weight:700;");
    }

    appendLog(message);
}

void MainWindow::refreshChromeWindows()
{
    if (!uiaReady_ || scanInProgress_)
        return;

    scanInProgress_ = true;
    sidebarRefreshButton_->setEnabled(false);
    dashboardRefreshButton_->setEnabled(false);
    statusBar()->showMessage("Scanning visible Chrome windows...");
    dashboardChromeSummary_->setText("Scanning Chrome windows...");
    appendLog("Chrome window scan started.");
    emit scanRequested();
}

void MainWindow::onChromeWindowsFound(const QJsonArray& windows)
{
    scanInProgress_ = false;
    sidebarRefreshButton_->setEnabled(true);
    dashboardRefreshButton_->setEnabled(true);

    chromeWindowCombo_->clear();
    if (windows.isEmpty())
    {
        chromeWindowCombo_->addItem("No Chrome windows detected", QString());
    }
    else
    {
        for (const QJsonValue& value : windows)
        {
            const QJsonObject item = value.toObject();
            const QString title = item.value("title").toString().isEmpty()
                ? "Chrome window"
                : item.value("title").toString();
            const QString hwnd = item.value("hwnd").toString();
            const QString label = QString("%1  ·  PID %2")
                .arg(title)
                .arg(item.value("processId").toInteger());
            chromeWindowCombo_->addItem(label, hwnd);
        }
    }

    const int count = windows.size();
    dashboardChromeSummary_->setText(
        QString("%1 Chrome window(s) detected").arg(count));
    inspectButton_->setEnabled(
        uiaReady_ && !chromeWindowCombo_->currentData().toString().isEmpty());

    statusBar()->showMessage(
        QString("Chrome scan complete · %1 visible window(s) detected").arg(count));
    appendLog(QString("Chrome window scan completed: %1 visible window(s) found.")
                  .arg(count));
}

quintptr MainWindow::selectedWindowHandle() const
{
    bool ok = false;
    const qulonglong value =
        chromeWindowCombo_->currentData().toString().toULongLong(&ok, 16);
    return ok ? static_cast<quintptr>(value) : 0;
}

void MainWindow::inspectSelectedWindow()
{
    const quintptr hwnd = selectedWindowHandle();
    if (!hwnd)
    {
        appendLog("Inspection skipped: no Chrome window is selected.");
        return;
    }

    inspectButton_->setEnabled(false);
    statusBar()->showMessage("Inspecting selected Chrome window through UI Automation...");
    appendLog("UIA inspection requested for HWND 0x"
        + QString::number(static_cast<qulonglong>(hwnd), 16).toUpper());
    emit inspectRequested(hwnd);
}

void MainWindow::onInspectionFinished(const QJsonObject& result)
{
    accessibilityTree_->setPlainText(
        QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Indented)));
    diagnosticsTabs_->setCurrentIndex(1);
    navigateTo(1);

    if (result.value("ok").toBool())
    {
        const int count = result.value("visitedNodes").toInt();
        statusBar()->showMessage(
            QString("UIA inspection complete · %1 nodes visited").arg(count));
        appendLog(QString("UIA inspection completed: %1 nodes visited.").arg(count));
    }
    else
    {
        const QString error = result.value("error").toString();
        statusBar()->showMessage("UIA inspection failed");
        appendLog("UIA inspection failed: " + error);
    }

    inspectButton_->setEnabled(
        uiaReady_ && !chromeWindowCombo_->currentData().toString().isEmpty());
}

void MainWindow::requestActiveTabPickup()
{
    appendLog("Active meeting tab pickup requested. Feature is not implemented in this build.");
    QMessageBox::information(
        this,
        "Active tab pickup is not available yet",
        "This build can discover Chrome windows and inspect their UI Automation tree.\n\n"
        "It does not yet identify the selected Chrome tab or register Zoom meetings. "
        "The next implementation step is to validate Chrome's selected TabItem through UI Automation.");
}

void MainWindow::appendLog(const QString& message)
{
    if (!liveDiagnostics_)
        return;

    const QString timestamp =
        QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
    liveDiagnostics_->appendPlainText(QString("[%1] %2").arg(timestamp, message));
}

void MainWindow::applyDarkProfessionalTheme()
{
    setStyleSheet(R"QSS(
        QWidget#appRoot, QWidget#pageRoot {
            background: #0B1423;
            color: #E5E7EB;
            font-family: "Segoe UI";
            font-size: 10pt;
        }
        QFrame#sidebar {
            background: #0D1A2B;
            border-right: 1px solid #26364A;
        }
        QLabel#brandLogo {
            color: white;
            background: #1677E8;
            border-radius: 11px;
            font-size: 25pt;
            font-weight: 700;
        }
        QLabel#brandName {
            color: #F8FAFC;
            font-size: 14pt;
            font-weight: 700;
        }
        QLabel#brandSubtitle, QLabel#pageSubtitle, QLabel#mutedText {
            color: #9BAEC4;
            font-size: 9pt;
        }
        QPushButton#sidebarNavButton {
            color: #D6E0EC;
            text-align: left;
            background: transparent;
            border: 1px solid transparent;
            border-radius: 8px;
            padding: 9px 10px;
            font-size: 10pt;
            font-weight: 600;
        }
        QPushButton#sidebarNavButton:hover:enabled {
            background: #172A42;
        }
        QPushButton#sidebarNavButton:checked {
            color: white;
            background: #1265D5;
            border: 1px solid #2783F5;
        }
        QPushButton#sidebarNavButton:disabled {
            color: #64748B;
        }
        QFrame#sidebarSeparator {
            background: #26364A;
            border: none;
        }
        QFrame#sidebarStatusCard {
            background: #112438;
            border: 1px solid #29425A;
            border-radius: 9px;
        }
        QLabel#statusDot { color: #22C55E; font-size: 12pt; }
        QLabel#sidebarStatusTitle { color: #E2FBEA; font-weight: 700; }
        QLabel#pageTitle {
            color: #F8FAFC;
            font-size: 24pt;
            font-weight: 700;
        }
        QLabel#sectionTitle {
            color: #EAF1F8;
            font-size: 13pt;
            font-weight: 700;
        }
        QFrame#metricCard {
            background: #17263A;
            border: 1px solid #2A4059;
            border-radius: 11px;
        }
        QLabel#metricHeading {
            color: #C7D4E4;
            font-size: 9pt;
        }
        QLabel#metricValue {
            color: #F8FAFC;
            font-size: 15pt;
            font-weight: 700;
        }
        QLabel#metricDetail {
            color: #8FA4BB;
            font-size: 8pt;
        }
        QPushButton {
            color: #E5EDF7;
            background: #1C2D43;
            border: 1px solid #354B66;
            border-radius: 8px;
            padding: 9px 13px;
            font-weight: 600;
        }
        QPushButton:hover:enabled { background: #263D59; }
        QPushButton:disabled {
            color: #65758A;
            background: #172335;
            border-color: #28394E;
        }
        QPushButton#primaryButton {
            color: white;
            background: #0878F9;
            border: 1px solid #278BFF;
            font-size: 10pt;
            font-weight: 700;
        }
        QPushButton#primaryButton:hover:enabled { background: #0867D8; }
        QPushButton#secondaryButton { background: #1D3048; }
        QPushButton#dangerButton {
            color: #FCA5A5;
            background: #291D29;
            border: 1px solid #9F3145;
        }
        QLabel#countPill {
            color: #93C5FD;
            background: #142D4B;
            border: 1px solid #27588D;
            border-radius: 9px;
            padding: 3px 8px;
            font-weight: 700;
        }
        QFrame#emptyMeetingsCard {
            background: #111F31;
            border: 1px dashed #3A506A;
            border-radius: 12px;
        }
        QLabel#emptyIcon {
            color: #6EAFFF;
            font-size: 30pt;
        }
        QLabel#emptyTitle {
            color: #E8F0FA;
            font-size: 15pt;
            font-weight: 700;
        }
        QFrame#panel {
            background: #17263A;
            border: 1px solid #2A4059;
            border-radius: 11px;
        }
        QLabel#panelHeading {
            color: #C7D4E4;
            font-size: 9pt;
            font-weight: 700;
            letter-spacing: 1px;
        }
        QLabel#settingsBody {
            color: #D4DFEC;
            font-size: 10pt;
        }
        QComboBox {
            color: #E5EDF7;
            background: #101D2D;
            border: 1px solid #354B66;
            border-radius: 7px;
            padding: 8px;
        }
        QComboBox QAbstractItemView {
            color: #E5EDF7;
            background: #17263A;
            selection-background-color: #145FC2;
        }
        QTabWidget::pane {
            background: #101D2D;
            border: 1px solid #2A4059;
            border-radius: 8px;
            top: -1px;
        }
        QTabBar::tab {
            color: #9BAEC4;
            background: #142338;
            border: 1px solid #2A4059;
            padding: 9px 14px;
            margin-right: 3px;
        }
        QTabBar::tab:selected {
            color: #F8FAFC;
            background: #203A58;
            border-bottom-color: #2587F7;
        }
        QPlainTextEdit {
            color: #C9D7E8;
            background: #0A1320;
            border: none;
            selection-background-color: #145FC2;
            font-family: Consolas;
            font-size: 9pt;
        }
        QScrollArea { background: transparent; border: none; }
        QScrollBar:vertical {
            background: #101D2D;
            width: 10px;
            margin: 2px;
        }
        QScrollBar::handle:vertical {
            background: #354B66;
            border-radius: 4px;
            min-height: 28px;
        }
        QStatusBar {
            color: #91A5BC;
            background: #0A1320;
            border-top: 1px solid #26364A;
        }
    )QSS");
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    event->accept();
}
