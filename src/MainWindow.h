#pragma once

#include <QMainWindow>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

class QCloseEvent;
class QComboBox;
class QFrame;
class QGridLayout;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QResizeEvent;
class QScrollArea;
class QStackedWidget;
class QTabWidget;
class QThread;
class UiaWorker;

class MainWindow final : public QMainWindow
{
    Q_OBJECT
    friend class MeetingControllerTests;

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

signals:
    void scanRequested();
    void inspectRequested(quintptr hwndValue);
    void pickupRequested();
    void activationRequested(quintptr hwndValue,
                             const QString& tabRuntimeId,
                             const QString& tabName,
                             const QString& requestKey);
    void microphoneReadRequested(quintptr hwndValue,
                                 const QString& tabRuntimeId,
                                 const QString& tabName,
                                 const QString& requestKey);
    void microphoneSetRequested(quintptr hwndValue,
                                const QString& tabRuntimeId,
                                const QString& tabName,
                                bool muted,
                                const QString& requestKey);
    void audioReadRequested(quintptr hwndValue,
                            const QString& tabRuntimeId,
                            const QString& tabName,
                            const QString& requestKey);
    void audioSetRequested(quintptr hwndValue,
                           const QString& tabRuntimeId,
                           const QString& tabName,
                           bool connected,
                           const QString& requestKey);
    void muteAllRequested(const QJsonArray& targets);
    void prepareImportedMeetingsRequested(const QJsonArray& targets);

private slots:
    void onInitialized(bool success, const QString& message);
    void onChromeWindowsFound(const QJsonArray& windows);
    void onInspectionFinished(const QJsonObject& result);
    void onChromeTabsListed(const QJsonObject& result);
    void onMeetingActivated(const QJsonObject& result);
    void onMicrophoneRead(const QJsonObject& result);
    void onMicrophoneSet(const QJsonObject& result);
    void onAudioConnectionRead(const QJsonObject& result);
    void onAudioConnectionSet(const QJsonObject& result);
    void onMuteAllFinished(const QJsonArray& results);
    void onImportedMeetingsPrepared(const QJsonArray& results);
    void refreshChromeWindows();
    void inspectSelectedWindow();
    void requestMeetingTabImport();
    void requestMuteAll();
    void releaseActiveMeeting();

protected:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    enum class Microphone
    {
        Unknown,
        Muted,
        Unmuted,
        AudioNotConnected
    };
    enum class Sound { Unknown, On, Off };

    struct ManagedMeeting
    {
        QString windowHandle;
        qint64 processId = 0;
        QString tabRuntimeId;
        QString title;
        QString host;
        QString meetingId;
        QString profileName;
        int windowNumber = 0;
        Microphone microphone = Microphone::Unknown;
        Sound sound = Sound::Unknown;

        QString key() const { return windowHandle + '/' + tabRuntimeId; }
        quintptr handle() const
        {
            return static_cast<quintptr>(windowHandle.toULongLong(nullptr, 16));
        }
    };

    QWidget* createPageBar();
    QWidget* createDashboardPage();
    QWidget* createDiagnosticsPage();
    QWidget* createSettingsPage();
    QWidget* createAboutPage();

    QFrame* createMeetingCard(const ManagedMeeting& meeting);
    void refreshMeetingCards();
    void layoutMeetingCards();
    void removeMeeting(const QString& key);
    void activateMeeting(const QString& key);
    void readMicrophone(const QString& key);
    void setMicrophone(const QString& key, bool muted);
    void readSound(const QString& key);
    void setSound(const QString& key, bool on);
    ManagedMeeting* findMeeting(const QString& key);
    void setMeetingBusy(bool busy, const QString& statusMessage);
    void applyMicrophoneResult(const QJsonObject& result, const QString& action);
    void applyAudioResult(const QJsonObject& result, const QString& action);
    void updateMeetingStates(ManagedMeeting& meeting, const QJsonObject& result);

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

    QPushButton* dashboardPickUpButton_ = nullptr;
    QPushButton* dashboardRefreshButton_ = nullptr;
    QPushButton* dashboardMuteAllButton_ = nullptr;
    QPushButton* releaseActiveButton_ = nullptr;

    QPushButton* inspectButton_ = nullptr;
    QComboBox* chromeWindowCombo_ = nullptr;
    QTabWidget* diagnosticsTabs_ = nullptr;
    QPlainTextEdit* liveDiagnostics_ = nullptr;
    QPlainTextEdit* accessibilityTree_ = nullptr;

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
    QGridLayout* meetingsLayout_ = nullptr;
    QScrollArea* meetingsArea_ = nullptr;
    QWidget* meetingEmptyCard_ = nullptr;

    QList<ManagedMeeting> meetings_;
    QList<QFrame*> meetingCards_;
    QString activeMeetingKey_;

    bool scanInProgress_ = false;
    bool pickupInProgress_ = false;
    bool meetingBusy_ = false;
};
