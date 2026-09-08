#include "secret-store.hpp"

#ifdef _WIN32
#include <windows.h>
#include <wincred.h>

#include <QByteArray>
#include <string>

namespace {
QString targetFor(const QString &key)
{
    return QStringLiteral("SocialCommentsOBS/") + key;
}
}
#endif

bool SecretStore::available()
{
#ifdef _WIN32
    return true;
#else
    return false;
#endif
}

QString SecretStore::load(const QString &key)
{
#ifdef _WIN32
    const std::wstring target = targetFor(key).toStdWString();
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential) || !credential)
        return {};

    QByteArray bytes(reinterpret_cast<const char *>(credential->CredentialBlob),
                     static_cast<int>(credential->CredentialBlobSize));
    const QString value = QString::fromUtf8(bytes);
    CredFree(credential);
    return value;
#else
    Q_UNUSED(key)
    return {};
#endif
}

bool SecretStore::save(const QString &key, const QString &value)
{
#ifdef _WIN32
    if (value.isEmpty())
        return remove(key);

    std::wstring target = targetFor(key).toStdWString();
    std::wstring username = L"SocialCommentsOBS";
    QByteArray bytes = value.toUtf8();

    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = target.data();
    credential.CredentialBlobSize = static_cast<DWORD>(bytes.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(bytes.data());
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    credential.UserName = username.data();

    return CredWriteW(&credential, 0) == TRUE;
#else
    Q_UNUSED(key)
    Q_UNUSED(value)
    return false;
#endif
}

bool SecretStore::remove(const QString &key)
{
#ifdef _WIN32
    const std::wstring target = targetFor(key).toStdWString();
    if (CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0) == TRUE)
        return true;
    return GetLastError() == ERROR_NOT_FOUND;
#else
    Q_UNUSED(key)
    return false;
#endif
}
