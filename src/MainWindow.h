#pragma once

#include <QMainWindow>
#include <QJsonArray>
#include <QJsonObject>

class QCloseEvent;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QTabWidget;
class QThread;
class UiaWorker;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

signals:
    void scanRequested();
    void inspectRequested(quintptr hwndValue);

private slots:
    void onInitialized(bool success, const QString& message);
    void onChromeWindowsFound(const QJsonArray& windows);
    void onInspectionFinished(const QJsonObject& result);
    void refreshChromeWindows();
    void inspectSelectedWindow();
    void requestActiveTabPickup();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    QWidget* createSidebar();
    QWidget* createDashboardPage();
    QWidget* createDiagnosticsPage();
    QWidget* createSettingsPage();
    QWidget* createAboutPage();

    void navigateTo(int pageIndex);
    void appendLog(const QString& message);
    void updateUiaStatus(bool ready, const QString& message);
    quintptr selectedWindowHandle() const;
    void applyDarkProfessionalTheme();

    QThread* workerThread_ = nullptr;
    UiaWorker* worker_ = nullptr;

    QWidget* dashboardPage_ = nullptr;
    QWidget* diagnosticsPage_ = nullptr;
    QWidget* settingsPage_ = nullptr;
    QWidget* aboutPage_ = nullptr;
    QStackedWidget* pageStack_ = nullptr;

    QPushButton* dashboardNavButton_ = nullptr;
    QPushButton* diagnosticsNavButton_ = nullptr;
    QPushButton* settingsNavButton_ = nullptr;
    QPushButton* aboutNavButton_ = nullptr;

    QPushButton* sidebarPickUpButton_ = nullptr;
    QPushButton* sidebarRefreshButton_ = nullptr;
    QPushButton* sidebarMuteAllButton_ = nullptr;

    QPushButton* dashboardPickUpButton_ = nullptr;
    QPushButton* dashboardRefreshButton_ = nullptr;
    QPushButton* dashboardMuteAllButton_ = nullptr;
    QPushButton* releaseActiveButton_ = nullptr;

    QPushButton* inspectButton_ = nullptr;
    QComboBox* chromeWindowCombo_ = nullptr;
    QTabWidget* diagnosticsTabs_ = nullptr;
    QPlainTextEdit* liveDiagnostics_ = nullptr;
    QPlainTextEdit* accessibilityTree_ = nullptr;

    QLabel* sidebarUiaState_ = nullptr;
    QLabel* sidebarUiaDetail_ = nullptr;
    QLabel* dashboardChromeSummary_ = nullptr;

    QLabel* totalMeetingsValue_ = nullptr;
    QLabel* totalMeetingsDetail_ = nullptr;
    QLabel* activeMeetingValue_ = nullptr;
    QLabel* activeMeetingDetail_ = nullptr;
    QLabel* safetyValue_ = nullptr;
    QLabel* safetyDetail_ = nullptr;
    QLabel* uiaValue_ = nullptr;
    QLabel* uiaDetail_ = nullptr;

    QLabel* meetingCountLabel_ = nullptr;
    QLabel* meetingEmptyLabel_ = nullptr;

    bool uiaReady_ = false;
    bool scanInProgress_ = false;
};
