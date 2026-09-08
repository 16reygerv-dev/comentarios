#pragma once

#include <QString>

class SecretStore {
public:
    static bool available();
    static QString load(const QString &key);
    static bool save(const QString &key, const QString &value);
    static bool remove(const QString &key);
};
