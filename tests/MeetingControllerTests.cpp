#include "MainWindow.h"
#include "UiaWorker.h"
#include "ZoomAudioState.h"

#include <QApplication>
#include <QFrame>
#include <QFontDatabase>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

using zoomAudio::Action;
using zoomAudio::Connection;

namespace
{
struct FakeAudio
{
    QList<QStringList> frames;
    QList<Action> actions;
    int frame = 0;
    bool canInvoke = true;

    zoomAudio::Backend backend()
    {
        return {
            [this] { return zoomAudio::observe(frames.at(qMin(frame, frames.size() - 1))); },
            [this](Action action) { actions.append(action); return canInvoke; },
            [this] { ++frame; }
        };
    }
};
}

class MeetingControllerTests : public QObject
{
    Q_OBJECT

    static void populate(MainWindow& window, MainWindow::Sound sound,
                         MainWindow::Microphone microphone)
    {
        // UI tests exercise card signals without calling UIA or touching Chrome.
        QObject::disconnect(&window, nullptr, window.worker_, nullptr);
        MainWindow::ManagedMeeting meeting;
        meeting.windowHandle = "1234";
        meeting.tabRuntimeId = "test-tab";
        meeting.title = "Audio test meeting";
        meeting.profileName = "Test profile";
        meeting.meetingId = "123456789";
        meeting.sound = sound;
        meeting.microphone = microphone;
        window.meetings_.append(meeting);
        window.activeMeetingKey_ = meeting.key();
        window.refreshMeetingCards();
    }

private slots:
    void initTestCase()
    {
        // The offscreen plugin has no Windows font discovery. Load the same
        // installed family the real Windows dashboard uses for visual checks.
        const QString fonts = qEnvironmentVariable("WINDIR") + "/Fonts/";
        QVERIFY(QFontDatabase::addApplicationFont(fonts + "segoeui.ttf") >= 0);
        QVERIFY(QFontDatabase::addApplicationFont(fonts + "segoeuib.ttf") >= 0);
        QVERIFY(QFontDatabase::addApplicationFont(fonts + "seguisb.ttf") >= 0);
    }

    void recognizesAudioControls_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<int>("action");
        QTest::addColumn<int>("connection");
        const auto row = [](const char* label, const QString& name, Action action, Connection connection)
        {
            QTest::newRow(label) << name << static_cast<int>(action) << static_cast<int>(connection);
        };
        row("muted mic", "Unmute", Action::None, Connection::Unknown);
        row("live mic", "Mute", Action::None, Connection::Unknown);
        row("shortcut", "Unmute (Alt+A)", Action::None, Connection::Unknown);
        row("my mic", "Mute my microphone", Action::None, Connection::Unknown);
        row("join", "Join Audio", Action::JoinAudio, Connection::Disconnected);
        row("computer join", "Join with Computer Audio", Action::JoinComputerAudio, Connection::Disconnected);
        row("alternate join", "Join Audio by Computer", Action::JoinComputerAudio, Connection::Disconnected);
        row("connect", "Connect to Computer Audio", Action::JoinComputerAudio, Connection::Disconnected);
        row("leave", "Leave Computer Audio", Action::LeaveAudio, Connection::Connected);
        row("disconnect", "Disconnect Audio", Action::LeaveAudio, Connection::Connected);
        row("menu", "Audio options", Action::OpenMenu, Connection::Unknown);
        row("menu with mute", "More mute controls", Action::OpenMenu, Connection::Unknown);
        row("participants", "Mute All", Action::None, Connection::Unknown);
        row("unmute participants", "Unmute All", Action::None, Connection::Unknown);
        row("another participant", "Mute Alice", Action::None, Connection::Unknown);
        row("auto join setting", "Automatically join computer audio", Action::None, Connection::Unknown);
        row("test speaker", "Test Speaker", Action::None, Connection::Unknown);
        row("speaker device", "Speakers (Realtek Audio)", Action::None, Connection::Unknown);
    }

    void recognizesAudioControls()
    {
        QFETCH(QString, name);
        QFETCH(int, action);
        QFETCH(int, connection);
        QCOMPARE(static_cast<int>(zoomAudio::actionForName(name)), action);
        QCOMPARE(static_cast<int>(zoomAudio::observe({name}).connection), connection);
    }

    void refusesContradictoryState()
    {
        FakeAudio fake{{{"Mute", "Join Audio"}}};
        const auto result = zoomAudio::setConnected(fake.backend(), false, 3);
        QVERIFY(!result.ok);
        QVERIFY(!result.actionAttempted);
        QVERIFY(fake.actions.isEmpty());
        QCOMPARE(result.observation.microphoneState, "unknown");
    }

    void alreadyConnectedDoesNotUnmuteMicrophone()
    {
        FakeAudio fake{{{"Unmute", "Leave Computer Audio"}}};
        const auto result = zoomAudio::setConnected(fake.backend(), true, 3);
        QVERIFY(result.ok);
        QVERIFY(!result.changed);
        QVERIFY(!result.actionAttempted);
        QCOMPARE(result.observation.microphoneState, "muted");
        QVERIFY(fake.actions.isEmpty());
    }

    void alreadyDisconnectedDoesNotJoin()
    {
        FakeAudio fake{{{"Join Audio"}}};
        const auto result = zoomAudio::setConnected(fake.backend(), false, 3);
        QVERIFY(result.ok);
        QVERIFY(!result.changed);
        QVERIFY(fake.actions.isEmpty());
    }

    void leavesThroughAudioMenuAndVerifies()
    {
        FakeAudio fake{{{"Unmute", "Audio options"},
                        {"Unmute", "Audio options", "Leave Computer Audio"},
                        {"Join Audio"}}};
        const auto result = zoomAudio::setConnected(fake.backend(), false, 5);
        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(fake.actions, QList<Action>({Action::OpenMenu, Action::LeaveAudio}));
        QCOMPARE(result.observation.connection, Connection::Disconnected);
        QCOMPARE(result.observation.microphoneState, "audioNotConnected");
    }

    void joinsThroughChooserAndVerifies()
    {
        FakeAudio fake{{{"Join Audio"}, {"Join Audio", "Join with Computer Audio"}, {},
                        {"Unmute", "Leave Computer Audio"}}};
        const auto result = zoomAudio::setConnected(fake.backend(), true, 6);
        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(fake.actions, QList<Action>({Action::JoinAudio, Action::JoinComputerAudio}));
        QCOMPARE(result.observation.connection, Connection::Connected);
        QCOMPARE(result.observation.microphoneState, "muted");
    }

    void joinsDirectlyWhenChooserAlreadyOpen()
    {
        FakeAudio fake{{{"Join with Computer Audio"}, {"Mute", "Leave Computer Audio"}}};
        const auto result = zoomAudio::setConnected(fake.backend(), true, 3);
        QVERIFY(result.ok);
        QCOMPARE(fake.actions, QList<Action>({Action::JoinComputerAudio}));
        QCOMPARE(result.observation.microphoneState, "unmuted");
    }

    void failedJoinDoesNotKeepClicking()
    {
        FakeAudio fake{{{"Join Audio"}, {"Join Audio", "Join with Computer Audio"}}};
        const auto result = zoomAudio::setConnected(fake.backend(), true, 5);
        QVERIFY(!result.ok);
        QVERIFY(result.actionAttempted);
        QVERIFY(!result.changed);
        QCOMPARE(fake.actions, QList<Action>({Action::JoinAudio, Action::JoinComputerAudio}));
        QVERIFY(result.error.contains("unverified"));
    }

    void joiningMustVerifyComputerAudioThroughMenu()
    {
        FakeAudio fake{{{"Join Audio"}, {"Unmute", "Audio options"},
                        {"Unmute", "Audio options", "Leave Computer Audio"}}};
        const auto result = zoomAudio::setConnected(fake.backend(), true, 5);
        QVERIFY(result.ok);
        QCOMPARE(fake.actions, QList<Action>({Action::JoinAudio, Action::OpenMenu}));
        QCOMPARE(result.observation.controlName, "Leave Computer Audio");
    }

    void microphoneAloneCannotVerifySoundOn()
    {
        FakeAudio fake{{{"Join Audio"}, {"Unmute"}}};
        const auto result = zoomAudio::setConnected(fake.backend(), true, 3);
        QVERIFY(!result.ok);
        QVERIFY(!result.changed);
        QCOMPARE(fake.actions, QList<Action>({Action::JoinAudio}));
    }

    void conflictingMicrophonesRemainUnread()
    {
        const auto observation = zoomAudio::observe({"Mute", "Unmute", "Leave Computer Audio"});
        QCOMPARE(observation.connection, Connection::Connected);
        QCOMPARE(observation.microphoneState, "unknown");
    }

    void failedLeaveDoesNotKeepClicking()
    {
        FakeAudio fake{{{"Mute", "Leave Computer Audio"}}};
        const auto result = zoomAudio::setConnected(fake.backend(), false, 5);
        QVERIFY(!result.ok);
        QVERIFY(!result.changed);
        QCOMPARE(fake.actions, QList<Action>({Action::LeaveAudio}));
    }

    void unknownStateNeverJoinsOrLeaves()
    {
        FakeAudio fake{{{"Audio options"}}};
        const auto result = zoomAudio::setConnected(fake.backend(), true, 3);
        QVERIFY(!result.ok);
        QVERIFY(!result.actionAttempted);
        QCOMPARE(fake.actions, QList<Action>({Action::OpenMenu}));
    }

    void missingLeaveControlDoesNotInvokeMicrophone()
    {
        FakeAudio fake{{{"Mute", "Audio options"}}};
        const auto result = zoomAudio::setConnected(fake.backend(), false, 3);
        QVERIFY(!result.ok);
        QVERIFY(!result.actionAttempted);
        QCOMPARE(fake.actions, QList<Action>({Action::OpenMenu}));
    }

    void unavailableActionIsReportedUnverified()
    {
        FakeAudio fake{{{"Join Audio"}}};
        fake.canInvoke = false;
        const auto result = zoomAudio::setConnected(fake.backend(), true, 3);
        QVERIFY(!result.ok);
        QVERIFY(result.actionAttempted);
        QVERIFY(result.error.contains("unverified"));
    }

    void readCanDiscoverConnectionThroughMenu()
    {
        FakeAudio fake{{{"Audio options"}, {"Leave Computer Audio"}}};
        const auto result = zoomAudio::readConnection(fake.backend(), 3);
        QVERIFY(result.ok);
        QVERIFY(!result.actionAttempted);
        QCOMPARE(result.observation.connection, Connection::Connected);
        QCOMPARE(fake.actions, QList<Action>({Action::OpenMenu}));
    }

    void soundOffButtonTargetsOnlyItsCardAndDisablesWhileBusy()
    {
        MainWindow window;
        populate(window, MainWindow::Sound::On, MainWindow::Microphone::Muted);
        QSignalSpy spy(&window, &MainWindow::audioSetRequested);
        auto* button = window.meetingCards_.first()->findChild<QPushButton*>("soundButton");
        QVERIFY(button);
        QCOMPARE(button->text(), "Sound Off");
        QVERIFY(button->toolTip().contains("microphone"));
        button->click();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toULongLong(), 0x1234ULL);
        QCOMPARE(spy.at(0).at(1).toString(), "test-tab");
        QCOMPARE(spy.at(0).at(3).toBool(), false);
        QCOMPARE(spy.at(0).at(4).toString(), window.meetings_.first().key());
        QVERIFY(window.meetingBusy_);
        QVERIFY(!window.meetingCards_.first()->findChild<QPushButton*>("soundButton")->isEnabled());
        window.setSound(window.meetings_.first().key(), false);
        QCOMPARE(spy.count(), 1);
    }

    void soundOnButtonReconnectsOnlyItsCard()
    {
        MainWindow window;
        populate(window, MainWindow::Sound::Off, MainWindow::Microphone::AudioNotConnected);
        QSignalSpy spy(&window, &MainWindow::audioSetRequested);
        auto* button = window.meetingCards_.first()->findChild<QPushButton*>("soundButton");
        QCOMPARE(button->text(), "Sound On");
        button->click();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(3).toBool(), true);
    }

    void secondCardDoesNotTargetFirstMeeting()
    {
        MainWindow window;
        populate(window, MainWindow::Sound::On, MainWindow::Microphone::Muted);
        auto second = window.meetings_.first();
        second.windowHandle = "5678";
        second.tabRuntimeId = "second-tab";
        second.title = "Second meeting";
        second.sound = MainWindow::Sound::Off;
        second.microphone = MainWindow::Microphone::AudioNotConnected;
        window.meetings_.append(second);
        window.refreshMeetingCards();
        QSignalSpy spy(&window, &MainWindow::audioSetRequested);
        window.meetingCards_.at(1)->findChild<QPushButton*>("soundButton")->click();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toULongLong(), 0x5678ULL);
        QCOMPARE(spy.at(0).at(1).toString(), "second-tab");
        QCOMPARE(spy.at(0).at(3).toBool(), true);
        QCOMPARE(window.meetings_.first().sound, MainWindow::Sound::On);
        QCOMPARE(window.meetings_.first().microphone, MainWindow::Microphone::Muted);
    }

    void missingSoundReadDoesNotDiscardReadableMicrophone()
    {
        MainWindow window;
        populate(window, MainWindow::Sound::Unknown, MainWindow::Microphone::Unknown);
        window.updateMeetingStates(window.meetings_.first(), {
            {"ok", true}, {"state", "muted"}, {"audioState", "unknown"},
            {"audioError", "Audio menu unavailable"}
        });
        QCOMPARE(window.meetings_.first().microphone, MainWindow::Microphone::Muted);
        QCOMPARE(window.meetings_.first().sound, MainWindow::Sound::Unknown);
    }

    void unknownSoundReadsBeforeOfferingChange()
    {
        MainWindow window;
        populate(window, MainWindow::Sound::Unknown, MainWindow::Microphone::Unknown);
        QSignalSpy readSpy(&window, &MainWindow::audioReadRequested);
        QSignalSpy setSpy(&window, &MainWindow::audioSetRequested);
        auto* button = window.meetingCards_.first()->findChild<QPushButton*>("soundButton");
        QCOMPARE(button->text(), "Read Sound");
        button->click();
        QCOMPARE(readSpy.count(), 1);
        QCOMPARE(setSpy.count(), 0);
    }

    void verifiedSoundResultRefreshesBothStatesAndIdentity()
    {
        MainWindow window;
        populate(window, MainWindow::Sound::On, MainWindow::Microphone::Muted);
        const QString oldKey = window.meetings_.first().key();
        window.onAudioConnectionSet({
            {"ok", true}, {"verified", true}, {"requestKey", oldKey},
            {"requestedConnected", false}, {"audioState", "disconnected"},
            {"audioControlName", "Join Audio"}, {"state", "audioNotConnected"},
            {"tabRuntimeId", "replacement-tab"}
        });
        QCOMPARE(window.meetings_.first().sound, MainWindow::Sound::Off);
        QCOMPARE(window.meetings_.first().microphone, MainWindow::Microphone::AudioNotConnected);
        QCOMPARE(window.activeMeetingKey_, window.meetings_.first().key());
        QCOMPARE(window.meetingCards_.first()->findChild<QPushButton*>("soundButton")->text(), "Sound On");
        QCOMPARE(window.meetingCards_.first()->findChild<QLabel*>("soundState")->text(), "Sound off");
        QCOMPARE(window.safetyValue_->text(), "Verified");
    }

    void unverifiedSoundResultClearsCachedStates()
    {
        MainWindow window;
        populate(window, MainWindow::Sound::On, MainWindow::Microphone::Muted);
        QTimer::singleShot(0, []
        {
            if (auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
                message->accept();
        });
        // Even an apparent success cannot update a card without verification.
        window.onAudioConnectionSet({
            {"ok", true}, {"verified", false}, {"requestedConnected", false},
            {"requestKey", window.meetings_.first().key()},
            {"audioState", "disconnected"}, {"state", "audioNotConnected"}
        });
        QCOMPARE(window.meetings_.first().sound, MainWindow::Sound::Unknown);
        QCOMPARE(window.meetings_.first().microphone, MainWindow::Microphone::Unknown);
        QCOMPARE(window.meetingCards_.first()->findChild<QPushButton*>("soundButton")->text(), "Read Sound");
        QCOMPARE(window.safetyValue_->text(), "Unread");
        QVERIFY(!window.meetingBusy_);
    }

    void rendersCardStates()
    {
        MainWindow window;
        populate(window, MainWindow::Sound::On, MainWindow::Microphone::Muted);
        auto disconnected = window.meetings_.first();
        disconnected.tabRuntimeId = "off-tab";
        disconnected.title = "Meeting with audio disconnected";
        disconnected.sound = MainWindow::Sound::Off;
        disconnected.microphone = MainWindow::Microphone::AudioNotConnected;
        window.meetings_.append(disconnected);
        auto unknown = disconnected;
        unknown.tabRuntimeId = "unknown-tab";
        unknown.title = "Meeting awaiting an audio read";
        unknown.sound = MainWindow::Sound::Unknown;
        unknown.microphone = MainWindow::Microphone::Unknown;
        window.meetings_.append(unknown);
        window.refreshMeetingCards();
        window.show();
        QTest::qWait(100);
        QVERIFY(window.grab().save("meeting-card-sound-states.png"));
        window.resize(1120, 720);
        QTest::qWait(100);
        QVERIFY(window.grab().save("meeting-card-sound-states-minimum.png"));
        QCOMPARE(window.meetingsArea_->horizontalScrollBar()->maximum(), 0);
        for (QFrame* card : window.meetingCards_)
            QVERIFY(card->width() <= window.meetingsArea_->viewport()->width());
    }
};

QTEST_MAIN(MeetingControllerTests)
#include "MeetingControllerTests.moc"
