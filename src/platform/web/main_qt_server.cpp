#include <QApplication>
#include "WebServerGui.h"
#include "../qt/ConfigController.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    // Create a lightweight ConfigController if available
    QGBA::ConfigController* cfg = nullptr;
    // If project has a ConfigController implementation in Core, user can wire it here.

    WebServerGui w(nullptr, cfg);
    w.show();
    return app.exec();
}
