#pragma once

#include <QUrl>
#include <QString>

class LocalClientDetector {
public:
    // Returns true if the provided URL points to the local machine.
    // If `reason` is provided, it will be filled with a human-readable explanation.
    static bool isLocal(const QUrl& url, QString* reason = nullptr);
};
