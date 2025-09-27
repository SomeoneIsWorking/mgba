#include "WebServerGui.h"


#include <QBoxLayout>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QDebug>
#include <QGridLayout>
#include <QScrollArea>
#include <QStatusBar>
#include <QSplitter>
#include <QKeyEvent>

#include "../qt/CoreManager.h"
#include "../qt/InputController.h"
#include "../qt/CoreController.h"
#include "../qt/ConfigController.h"
#include "../qt/MultiplayerController.h"
#include "StreamingCommon.h"
#include <mgba/internal/gba/input.h>

using namespace QGBA;

WebServerGui::LocalSession::~LocalSession() {}

static QStringList getAvailableDevices(InputController* dummyCtrl) {
    QStringList list;
    if (!dummyCtrl) return list;
    // Add keyboard as an option
    list << QObject::tr("Keyboard");
    // Query gamepads drivers and connected names
    auto drivers = dummyCtrl->connectedGamepads();
    for (const QString& name : drivers)
        list << name;
    return list;
}

WebServerGui::WebServerGui(QWidget* parent, ConfigController* config, CoreManager* coreManager, MultiplayerController* multiplayer)
    : QWidget(parent), m_coreManager(coreManager), m_config(config), m_ownsCoreManager(false), m_ownsMultiplayer(false), m_multiplayer(multiplayer) {
    setupUi();
    if (!m_coreManager) {
        m_coreManager = new CoreManager();
        m_ownsCoreManager = true;
    }
    if (m_config)
        m_coreManager->setConfig(m_config->config());

    if (!m_multiplayer) {
        m_multiplayer = new MultiplayerController();
        m_ownsMultiplayer = true;
    }

    m_frameTimer = new QTimer(this);
    connect(m_frameTimer, &QTimer::timeout, this, &WebServerGui::onFrameTimer);
    m_frameTimer->start(16);
}

WebServerGui::~WebServerGui() {
    // stop and delete all sessions
    for (auto& sp : m_sessions) {
        if (!sp) continue;
        closeSession(sp.get());
    }
    m_sessions.clear();

    if (m_ownsMultiplayer && m_multiplayer) { delete m_multiplayer; m_multiplayer = nullptr; }
    if (m_ownsCoreManager && m_coreManager) { delete m_coreManager; m_coreManager = nullptr; }
}

void WebServerGui::setupUi() {
    auto* lay = new QVBoxLayout(this);

    // Central area: vertical splitter for two views (top/bottom)
    m_viewsSplitter = new QSplitter(Qt::Horizontal, this);
    m_viewsSplitter->setChildrenCollapsible(false);
    m_viewsSplitter->setHandleWidth(6);
    lay->addWidget(m_viewsSplitter);

    // Real status bar at bottom for compact controls
    QStatusBar* statusBar = new QStatusBar(this);
    statusBar->setFixedHeight(40);
    statusBar->setStyleSheet("QStatusBar{font-size:11px;} QPushButton{font-size:10px; padding:1px 4px; margin:0px;} QLabel{font-size:11px;} ");
    m_createBtn = new QPushButton(tr("+"), this);
    m_stopAllBtn = new QPushButton(tr("Stop"), this);
    m_createBtn->setFixedSize(20,18);
    m_stopAllBtn->setFixedSize(40,18);
    statusBar->addWidget(m_createBtn);
    statusBar->addWidget(m_stopAllBtn);
    QWidget* sessionControls = new QWidget(statusBar);
    auto* sessionControlsLay = new QHBoxLayout(sessionControls);
    sessionControlsLay->setSpacing(4);
    sessionControlsLay->setContentsMargins(0,0,0,0);
    sessionControls->setLayout(sessionControlsLay);
    statusBar->addPermanentWidget(sessionControls, 1);
    lay->addWidget(statusBar);

    // Install application-level event filter so we can capture arrow keys and
    // other key events even when child widgets don't forward them to
    // WebServerGui::keyPressEvent.
    qApp->installEventFilter(this);

    connect(m_createBtn, &QPushButton::clicked, this, [this, sessionControlsLay]() {
        QString rom = QString::fromUtf8("kirby.gba");
        auto sp = std::make_unique<LocalSession>();
        LocalSession* s = sp.get();
        s->sessionName = QString::fromUtf8("Local %1").arg(m_sessions.size() + 1);

    // create view (plain QLabel); we'll install an event filter after input/core are ready
    QLabel* view = new QLabel(this);
        view->setAlignment(Qt::AlignCenter);
        view->setMinimumSize(160, 128);
        view->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        s->view = view;

        // device combo
        QComboBox* cb = new QComboBox(this);

        // per-session control widget
        QWidget* ctrl = new QWidget(this);
        auto* ctrlLay = new QHBoxLayout(ctrl);
        ctrlLay->setContentsMargins(2,0,2,0);
        ctrlLay->setSpacing(4);
        QLabel* lbl = new QLabel(s->sessionName, ctrl);
        lbl->setFixedHeight(18);
        ctrlLay->addWidget(lbl);
        cb->setFixedHeight(18);
        cb->setFixedWidth(120);
        ctrlLay->addWidget(cb);
        QPushButton* removeBtn = new QPushButton(tr("x"), ctrl);
        removeBtn->setFixedSize(18,18);
        ctrlLay->addWidget(removeBtn);
        sessionControlsLay->addWidget(ctrl);

        // probe devices
        InputController* probe = new InputController(nullptr, this);
        QStringList devs = getAvailableDevices(probe);
        delete probe;
        cb->addItems(devs);
    cb->setFocusPolicy(Qt::NoFocus);
    s->deviceCombo = cb;

        // Start core controller for this session
        if (m_coreManager) {
            s->core = m_coreManager->loadGame(rom);
            if (s->core) {
                s->input = new InputController(nullptr, this);
                s->core->setInputController(s->input);
                if (m_config) s->core->loadConfig(m_config);
                connect(s->core, &CoreController::frameAvailable, this, [s]() {
                    QMutexLocker lk(&s->frameMutex);
                    s->latestFrame = s->core->getPixels();
                });
                if (m_multiplayer) {
                    if (!m_multiplayer->attachGame(s->core)) {
                        qDebug() << "Failed to attach core to multiplayer controller";
                    }
                }
                s->core->start();
                s->core->setSync(false);
                if (m_multiplayer) {
                    s->playerId = m_multiplayer->playerId(s->core);
                }
                if (s->playerId == 0) {
                    s->core->setPaused(true);
                    s->frameTimer = new QTimer(this);
                    s->frameTimer->setInterval(1000 / 60);
                    connect(s->frameTimer, &QTimer::timeout, this, [s]() { s->core->frameAdvance(); });
                    s->frameTimer->start();
                }
            }
        }

        // store and add view
        m_sessions.push_back(std::move(sp));
        LocalSession* ls = m_sessions.back().get();
        if (ls->view) {
            m_viewsSplitter->addWidget(ls->view);
            for (int i = 0; i < m_viewsSplitter->count(); ++i) m_viewsSplitter->setStretchFactor(i, 1);
        }

        // update label to include player and device
        lbl->setText(QString("P%1: %2 - %3").arg(ls->playerId >= 0 ? ls->playerId : -1).arg(ls->sessionName).arg(cb->currentText()));

        // wire device change -> forward to member function so we can reuse it and call once on init
        connect(cb, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, ls, cb, lbl](int index) {
            applyDeviceBindings(ls, cb, lbl, index);
        });

        // Call once to apply initial bindings (combo default)
        applyDeviceBindings(ls, cb, lbl, cb->currentIndex());

        // remove handler
        connect(removeBtn, &QPushButton::clicked, this, [this, ls, ctrl]() {
            closeSession(ls);
            ctrl->deleteLater();
            for (auto it = m_sessions.begin(); it != m_sessions.end(); ++it) { if (it->get() == ls) { m_sessions.erase(it); break; } }
            // rebuild splitter
            while (m_viewsSplitter->count()) { QWidget* w = m_viewsSplitter->widget(0); w->setParent(nullptr); }
            for (auto& sp2 : m_sessions) if (sp2 && sp2->view) m_viewsSplitter->addWidget(sp2->view);
            for (int i = 0; i < m_viewsSplitter->count(); ++i) m_viewsSplitter->setStretchFactor(i, 1);
        });
    });

    connect(m_stopAllBtn, &QPushButton::clicked, this, [this]() {
        for (auto& sp : m_sessions) {
            if (!sp) continue;
            closeSession(sp.get());
        }
        m_sessions.clear();
    });
}

void WebServerGui::keyPressEvent(QKeyEvent* event) {
    if (event->isAutoRepeat()) { QWidget::keyPressEvent(event); return; }
    bool handled = false;
    for (auto& sp : m_sessions) {
        if (!sp) continue;
        auto ls = sp.get();
        if (!ls || !ls->input || !ls->core || !ls->deviceCombo) continue;
        // deviceCombo index 0 == Keyboard
        if (ls->deviceCombo->currentIndex() != 0) continue;
        int gbaKey = ls->input->mapKeyboard(event->key());
        if (gbaKey == -1) continue;
        ls->core->addKey(gbaKey);
        handled = true;
    }
    if (handled) {
        event->accept();
    } else {
        QWidget::keyPressEvent(event);
    }
}

void WebServerGui::keyReleaseEvent(QKeyEvent* event) {
    if (event->isAutoRepeat()) { QWidget::keyReleaseEvent(event); return; }
    bool handled = false;
    for (auto& sp : m_sessions) {
        if (!sp) continue;
        auto ls = sp.get();
        if (!ls || !ls->input || !ls->core || !ls->deviceCombo) continue;
        if (ls->deviceCombo->currentIndex() != 0) continue;
        int gbaKey = ls->input->mapKeyboard(event->key());
        if (gbaKey == -1) continue;
        ls->core->clearKey(gbaKey);
        handled = true;
    }
    if (handled) {
        event->accept();
    } else {
        QWidget::keyReleaseEvent(event);
    }
}

bool WebServerGui::eventFilter(QObject* obj, QEvent* event) {
    // Only handle key press/release events, forward them to keyboard sessions
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent* ke = static_cast<QKeyEvent*>(event);
        if (ke->isAutoRepeat()) return QWidget::eventFilter(obj, event);
        for (auto& sp : m_sessions) {
            if (!sp) continue;
            auto ls = sp.get();
            if (!ls || !ls->input || !ls->core || !ls->deviceCombo) continue;
            if (ls->deviceCombo->currentIndex() != 0) continue;
            int gbaKey = ls->input->mapKeyboard(ke->key());
            if (gbaKey == -1) continue;
            ls->core->addKey(gbaKey);
        }
        // don't swallow the event; return false so other consumers also receive it
        return false;
    } else if (event->type() == QEvent::KeyRelease) {
        QKeyEvent* ke = static_cast<QKeyEvent*>(event);
        if (ke->isAutoRepeat()) return QWidget::eventFilter(obj, event);
        for (auto& sp : m_sessions) {
            if (!sp) continue;
            auto ls = sp.get();
            if (!ls || !ls->input || !ls->core || !ls->deviceCombo) continue;
            if (ls->deviceCombo->currentIndex() != 0) continue;
            int gbaKey = ls->input->mapKeyboard(ke->key());
            if (gbaKey == -1) continue;
            ls->core->clearKey(gbaKey);
        }
        return false;
    }
    return QWidget::eventFilter(obj, event);
}

void WebServerGui::applyDeviceBindings(LocalSession* ls, QComboBox* cb, QLabel* lbl, int index) {
    if (!ls || !ls->input) return;
    // Our combo box items are: ["Keyboard", <gamepad0>, <gamepad1>, ...]
    if (index == 0) {
        // Keyboard selected: apply requested keyboard bindings (Z,X,A,S,Enter,Tab)
        auto mapper = ls->input->mapper(InputController::KEYBOARD);
        mapper.unbindAllKeys();
        // Map GBA buttons to Qt keys: A, B, L, R, START, SELECT
        // bindKey takes (platformKey, gbaKey) at the C level; call accordingly
        mapper.bindKey(Qt::Key_Z, GBA_KEY_A);
        mapper.bindKey(Qt::Key_X, GBA_KEY_B);
        mapper.bindKey(Qt::Key_A, GBA_KEY_L);
        mapper.bindKey(Qt::Key_S, GBA_KEY_R);
        mapper.bindKey(Qt::Key_Return, GBA_KEY_START);
        mapper.bindKey(Qt::Key_Tab, GBA_KEY_SELECT);
            // D-pad
            mapper.bindKey(Qt::Key_Up, GBA_KEY_UP);
            mapper.bindKey(Qt::Key_Down, GBA_KEY_DOWN);
            mapper.bindKey(Qt::Key_Left, GBA_KEY_LEFT);
            mapper.bindKey(Qt::Key_Right, GBA_KEY_RIGHT);
    } else {
        // Controller selected: set active gamepad (subtract 1 because index 0 is Keyboard)
        ls->input->setGamepad(index - 1);
        // Apply the driver's default bindings (standard layout) if available
        if (auto driver = ls->input->gamepadDriver()) {
            driver->bindDefaults(ls->input);
        }
    }
    if (lbl) lbl->setText(QString("P%1: %2 - %3").arg(ls->playerId >= 0 ? ls->playerId : -1).arg(ls->sessionName).arg(cb ? cb->currentText() : QString()));
}

void WebServerGui::teardownUi() {
    // ...existing code...
}

void WebServerGui::attachCoreSignals() {
    // no-op: handled per-session in the new multi-instance flow
}

void WebServerGui::onStartClicked() {
    // kept for compatibility with header; forward to create button handler
    m_createBtn->click();
}

void WebServerGui::onStopClicked() {
    // kept for compatibility with header; forward to stop-all
    m_stopAllBtn->click();
}

void WebServerGui::onFrameTimer() {
    for (auto& sp : m_sessions) {
        if (!sp) continue;
        LocalSession* s = sp.get();
        QImage frame;
        {
            QMutexLocker lk(&s->frameMutex);
            if (s->latestFrame.isNull()) continue;
            frame = s->latestFrame.copy();
            s->latestFrame = QImage();
        }
        if (!frame.isNull() && s->view) {
            QPixmap pm = QPixmap::fromImage(frame).scaled(s->view->size(), Qt::KeepAspectRatio);
            s->view->setPixmap(pm);
        }
    }
}

#include <algorithm>

void WebServerGui::closeSession(LocalSession* s) {
    if (!s) return;

    if (s->frameTimer) {
        s->frameTimer->stop();
        delete s->frameTimer;
        s->frameTimer = nullptr;
    }

    if (s->core) {
        s->core->stop();
        delete s->core;
        s->core = nullptr;
    }

    if (s->input) {
        delete s->input;
        s->input = nullptr;
    }
    if (s->view) {
        s->view->setParent(nullptr);
        delete s->view;
        s->view = nullptr;
    }
}

void WebServerGui::closeEvent(QCloseEvent* event) {
    // Ensure all sessions are fully stopped before the window closes. This
    // prevents the window teardown from racing with per-core shutdown which
    // can call into multiplayer detach logic.
    for (auto& sp : m_sessions) {
        if (!sp) continue;
        closeSession(sp.get());
    }
    m_sessions.clear();
    event->accept();
}
