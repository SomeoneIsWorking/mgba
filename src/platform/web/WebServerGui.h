#pragma once

#include <QWidget>
#include <QImage>
#include <QMutex>
#include <QTimer>
#include <QVector>
#include <memory>
#include <vector>

class QComboBox;
class QSplitter;
class QScrollArea;

namespace QGBA {
class CoreManager;
class CoreController;
class InputController;
class MultiplayerController;
class ConfigController;
}

class QLabel;
class QPushButton;
class QObject;

namespace QGBA {
// Simple GUI to host a single emulator instance and display frames/audio.
class WebServerGui : public QWidget {
    Q_OBJECT
public:
    // If coreManager or multiplayer are provided the GUI will use them but won't take ownership.
    explicit WebServerGui(QWidget* parent = nullptr, QGBA::ConfigController* config = nullptr,
                         QGBA::CoreManager* coreManager = nullptr, QGBA::MultiplayerController* multiplayer = nullptr);
    ~WebServerGui() override;

    // Load ROM and start the emulator (synchronous convenience wrapper)
    bool startEmulator(const QString& romPath);
    void stopEmulator();

signals:
    void logMessage(const QString& msg);

private slots:
    void onStartClicked();
    void onStopClicked();
    void onFrameTimer();

private:
    void setupUi();
    void teardownUi();
    void attachCoreSignals();
    void closeEvent(QCloseEvent* event) override;

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;


    QGBA::CoreManager* m_coreManager = nullptr;
    QGBA::ConfigController* m_config = nullptr;

    bool m_ownsCoreManager = false;
    bool m_ownsMultiplayer = false;

    struct LocalSession {
        QGBA::CoreController* core = nullptr;
        QGBA::InputController* input = nullptr;
        QLabel* view = nullptr;
        QComboBox* deviceCombo = nullptr;
        QImage latestFrame;
        QMutex frameMutex;
        QString sessionName;
        int playerId = -1;
        QTimer* frameTimer = nullptr;
        ~LocalSession();
    };

    void applyDeviceBindings(LocalSession* ls, QComboBox* cb, QLabel* lbl, int index);
    void closeSession(LocalSession* s);

    std::vector<std::unique_ptr<LocalSession>> m_sessions;

    // UI
    QScrollArea* m_scroll = nullptr;
    QWidget* m_viewsWidget = nullptr;
    QSplitter* m_viewsSplitter = nullptr;
    QPushButton* m_createBtn = nullptr;
    QPushButton* m_stopAllBtn = nullptr;

    QTimer* m_frameTimer = nullptr;
    int m_frameW = 240;
    int m_frameH = 160;
    QGBA::MultiplayerController* m_multiplayer = nullptr;
};
} // namespace QGBA
