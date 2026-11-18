#include "MainWindow.h"

#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("Meeting Controller");
    resize(1100, 720);

    auto* central = new QWidget(this);
    auto* rootLayout = new QVBoxLayout(central);

    auto* buttonLayout = new QHBoxLayout();

    launchButton_ = new QPushButton("Launch Managed Chrome", central);
    refreshButton_ = new QPushButton("Refresh Tabs", central);
    inspectButton_ = new QPushButton("Inspect Selected Tab", central);

    refreshButton_->setEnabled(false);
    inspectButton_->setEnabled(false);

    buttonLayout->addWidget(launchButton_);
    buttonLayout->addWidget(refreshButton_);
    buttonLayout->addWidget(inspectButton_);
    buttonLayout->addStretch();

    statusLabel_ = new QLabel(
        "Not connected. Launch the dedicated Chrome profile to begin.", central);

    auto* safetyLabel = new QLabel(
        "Feasibility build: microphone mute/unmute is intentionally disabled "
        "until Zoom's actual controls and state semantics are verified.",
        central);
    safetyLabel->setWordWrap(true);
    safetyLabel->setStyleSheet(
        "QLabel { background: #fff4d6; color: #6b4f00; "
        "padding: 8px; border: 1px solid #e5c76b; border-radius: 4px; }");

    tabsTable_ = new QTableWidget(0, 3, central);
    tabsTable_->setHorizontalHeaderLabels(
        {"Page title", "URL", "CDP target ID"});
    tabsTable_->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::ResizeToContents);
    tabsTable_->horizontalHeader()->setSectionResizeMode(
        1, QHeaderView::Stretch);
    tabsTable_->horizontalHeader()->setSectionResizeMode(
        2, QHeaderView::ResizeToContents);
    tabsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    tabsTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    tabsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);

    output_ = new QTextEdit(central);
    output_->setReadOnly(true);
    output_->setPlaceholderText(
        "Inspection results and diagnostics will appear here.");

    rootLayout->addLayout(buttonLayout);
    rootLayout->addWidget(statusLabel_);
    rootLayout->addWidget(safetyLabel);
    rootLayout->addWidget(new QLabel("Managed Chrome tabs", central));
    rootLayout->addWidget(tabsTable_, 3);
    rootLayout->addWidget(new QLabel("Diagnostics / selected-page button inventory", central));
    rootLayout->addWidget(output_, 2);

    setCentralWidget(central);

    connect(launchButton_, &QPushButton::clicked,
            this, &MainWindow::launchManagedChrome);
    connect(refreshButton_, &QPushButton::clicked,
            this, &MainWindow::refreshTabs);
    connect(inspectButton_, &QPushButton::clicked,
            this, &MainWindow::inspectSelectedTab);

    connect(tabsTable_, &QTableWidget::itemSelectionChanged, this, [this]()
    {
        inspectButton_->setEnabled(tabsTable_->currentRow() >= 0 && cdp_);
    });
}

MainWindow::~MainWindow() = default;

QString MainWindow::findChromeExecutable() const
{
    const QString localAppData =
        qEnvironmentVariable("LOCALAPPDATA");
    const QString programFiles =
        qEnvironmentVariable("PROGRAMFILES");
    const QString programFilesX86 =
        qEnvironmentVariable("PROGRAMFILES(X86)");

    const QStringList candidates = {
        programFiles + "/Google/Chrome/Application/chrome.exe",
        programFilesX86 + "/Google/Chrome/Application/chrome.exe",
        localAppData + "/Google/Chrome/Application/chrome.exe"
    };

    for (const QString& candidate : candidates)
    {
        if (QFileInfo::exists(candidate))
            return QDir::toNativeSeparators(candidate);
    }

    return QString();
}

void MainWindow::launchManagedChrome()
{
    if (cdp_ && cdp_->isRunning())
    {
        QMessageBox::information(
            this, "Meeting Controller",
            "The managed Chrome instance is already running.");
        return;
    }

    chromeExecutable_ = findChromeExecutable();

    if (chromeExecutable_.isEmpty())
    {
        chromeExecutable_ = QFileDialog::getOpenFileName(
            this,
            "Locate Google Chrome",
            QString(),
            "Chrome executable (chrome.exe)");
    }

    if (chromeExecutable_.isEmpty())
        return;

    const QString appData =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);

    profileDirectory_ = QDir(appData).filePath("chrome-profile");

    if (!QDir().mkpath(profileDirectory_))
    {
        QMessageBox::critical(
            this, "Meeting Controller",
            "Could not create the dedicated Chrome profile directory.");
        return;
    }

    if (cdp_)
    {
        cdp_->deleteLater();
        cdp_ = nullptr;
    }

    cdp_ = new CdpClient(this);

    connect(cdp_, &CdpClient::errorOccurred, this, [this](const QString& message)
    {
        appendLog("[CDP] " + message);
        statusLabel_->setText("CDP warning: " + message);
    });

    connect(cdp_, &CdpClient::eventReceived, this,
            [this](const QJsonObject& event)
    {
        const QString method = event.value("method").toString();
        if (!method.isEmpty())
            appendLog("[CDP event] " + method);
    });

    QString error;
    if (!cdp_->start(chromeExecutable_, profileDirectory_, &error))
    {
        QMessageBox::critical(this, "Chrome launch failed", error);
        cdp_->deleteLater();
        cdp_ = nullptr;
        return;
    }

    launchButton_->setEnabled(false);
    refreshButton_->setEnabled(true);
    statusLabel_->setText(
        "Managed Chrome launched. Sign in and join a test Zoom meeting.");

    appendLog("Chrome executable: " + chromeExecutable_);
    appendLog("Dedicated profile: " + profileDirectory_);
    appendLog("CDP transport: inherited Windows anonymous pipes.");

    QTimer::singleShot(1800, this, &MainWindow::refreshTabs);
}

void MainWindow::refreshTabs()
{
    if (!cdp_ || !cdp_->isRunning())
    {
        statusLabel_->setText("Managed Chrome is not running.");
        return;
    }

    statusLabel_->setText("Discovering Chrome tabs...");

    cdp_->refreshTargets([this](const QJsonArray& targets)
    {
        tabsTable_->setRowCount(0);

        for (const QJsonValue& value : targets)
        {
            const QJsonObject target = value.toObject();

            if (target.value("type").toString() != "page")
                continue;

            const int row = tabsTable_->rowCount();
            tabsTable_->insertRow(row);

            const QString title = target.value("title").toString();
            const QString url = target.value("url").toString();
            const QString targetId = target.value("targetId").toString();

            auto* titleItem = new QTableWidgetItem(title);
            titleItem->setData(Qt::UserRole, targetId);

            tabsTable_->setItem(row, 0, titleItem);
            tabsTable_->setItem(row, 1, new QTableWidgetItem(url));
            tabsTable_->setItem(row, 2, new QTableWidgetItem(targetId));
        }

        statusLabel_->setText(
            QString("Found %1 page tab(s) in the managed Chrome instance.")
                .arg(tabsTable_->rowCount()));

        inspectButton_->setEnabled(tabsTable_->rowCount() > 0);
        appendLog(QString("Target discovery completed: %1 page(s).")
                      .arg(tabsTable_->rowCount()));
    });
}

void MainWindow::inspectSelectedTab()
{
    if (!cdp_)
        return;

    const int row = tabsTable_->currentRow();
    if (row < 0)
    {
        QMessageBox::information(
            this, "Meeting Controller", "Select a Chrome tab first.");
        return;
    }

    const QString targetId =
        tabsTable_->item(row, 0)->data(Qt::UserRole).toString();

    if (targetId.isEmpty())
    {
        appendLog("Selected row does not have a CDP target ID.");
        return;
    }

    statusLabel_->setText("Inspecting visible controls in selected page...");
    inspectButton_->setEnabled(false);

    cdp_->inspectTargetButtons(targetId,
        [this](const QJsonObject& result)
        {
            output_->setPlainText(
                QString::fromUtf8(
                    QJsonDocument(result).toJson(QJsonDocument::Indented)));

            statusLabel_->setText(
                result.value("ok").toBool()
                    ? "Read-only page inspection completed."
                    : "Page inspection failed. See diagnostics.");

            inspectButton_->setEnabled(tabsTable_->currentRow() >= 0);
        });
}

void MainWindow::appendLog(const QString& text)
{
    output_->append(text.toHtmlEscaped());
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (cdp_ && cdp_->isRunning())
    {
        const auto answer = QMessageBox::question(
            this,
            "Close Meeting Controller?",
            "Closing Meeting Controller will close its managed Chrome instance "
            "and disconnect meetings open in that browser profile.\n\nContinue?",
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel);

        if (answer != QMessageBox::Yes)
        {
            event->ignore();
            return;
        }

        statusLabel_->setText("Closing managed Chrome...");
        cdp_->shutdownBrowser();
    }

    event->accept();
}
