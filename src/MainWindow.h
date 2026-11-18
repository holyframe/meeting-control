#pragma once

#include "CdpClient.h"

#include <QMainWindow>

class QCloseEvent;
class QLabel;
class QPushButton;
class QTableWidget;
class QTextEdit;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void launchManagedChrome();
    void refreshTabs();
    void inspectSelectedTab();

private:
    QString findChromeExecutable() const;
    void appendLog(const QString& text);

    CdpClient* cdp_ = nullptr;

    QPushButton* launchButton_ = nullptr;
    QPushButton* refreshButton_ = nullptr;
    QPushButton* inspectButton_ = nullptr;

    QLabel* statusLabel_ = nullptr;
    QTableWidget* tabsTable_ = nullptr;
    QTextEdit* output_ = nullptr;

    QString chromeExecutable_;
    QString profileDirectory_;
};
