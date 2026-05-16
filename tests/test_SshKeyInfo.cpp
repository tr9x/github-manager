// Unit tests for ghm::git::inspectSshKey().
//
// We don't generate real SSH keys in tests (would need ssh-keygen,
// extra process management, fixture juggling). Instead we write
// the file headers that ssh-keygen produces — those are stable
// across OpenSSH versions and our parser only looks at the headers.
//
// Sample headers were captured from ssh-keygen output (OpenSSH 9.x);
// the base64 bodies are truncated to just enough bytes to parse
// the cipher name out, since that's all our inspectSshKey() needs.

#include <QtTest>
#include <QObject>
#include <QTemporaryFile>

#include "git/SshKeyInfo.h"

using ghm::git::inspectSshKey;

class TestSshKeyInfo : public QObject {
    Q_OBJECT

private:
    // Helper: write `contents` to a temp file, return its path. The
    // QTemporaryFile must remain in scope for the test, so we let
    // callers manage that.
    QString writeTemp(QTemporaryFile& tmp, const QByteArray& contents)
    {
        if (!tmp.open()) return {};
        tmp.write(contents);
        tmp.close();
        return tmp.fileName();
    }

private slots:

    void nonexistentFile_isInvalid()
    {
        auto info = inspectSshKey(QStringLiteral("/nonexistent/path/key"));
        QVERIFY(!info.valid());
        QVERIFY(!info.exists);
    }

    void emptyFile_isValidButNotEncrypted()
    {
        QTemporaryFile tmp;
        const QString path = writeTemp(tmp, "");
        auto info = inspectSshKey(path);
        QVERIFY(info.valid());
        QVERIFY(!info.encrypted);
    }

    void unencrypted_pem_rsa()
    {
        // Classic PEM RSA, no Proc-Type header → unencrypted.
        QTemporaryFile tmp;
        const QByteArray body =
            "-----BEGIN RSA PRIVATE KEY-----\n"
            "MIIEpAIBAAKCAQEAyfakebodyhere==\n"
            "-----END RSA PRIVATE KEY-----\n";
        const QString path = writeTemp(tmp, body);
        auto info = inspectSshKey(path);
        QVERIFY(info.valid());
        QVERIFY(!info.encrypted);
    }

    void encrypted_pem_rsa()
    {
        // Classic PEM RSA with Proc-Type: 4,ENCRYPTED header.
        QTemporaryFile tmp;
        const QByteArray body =
            "-----BEGIN RSA PRIVATE KEY-----\n"
            "Proc-Type: 4,ENCRYPTED\n"
            "DEK-Info: AES-128-CBC,1234567890ABCDEF\n"
            "\n"
            "MIIEpAIBAAKCAQEAyfakebodyhere==\n"
            "-----END RSA PRIVATE KEY-----\n";
        const QString path = writeTemp(tmp, body);
        auto info = inspectSshKey(path);
        QVERIFY(info.valid());
        QVERIFY(info.encrypted);
    }

    void unencrypted_openssh()
    {
        // OpenSSH key with cipher "none". The base64 below decodes to:
        //   "openssh-key-v1\0"                  (15 bytes)
        //   <uint32 big-endian: 4>              (length of "none")
        //   "none"                              (cipher name)
        //   ... (rest doesn't matter for our parser)
        //
        // Constructed manually:
        //   00..0e: "openssh-key-v1\0"
        //   0f..12: 0x00 0x00 0x00 0x04
        //   13..16: "none"
        // Total 23 bytes → base64 = "b3BlbnNzaC1rZXktdjEAAAAABG5vbmU="
        QTemporaryFile tmp;
        const QByteArray body =
            "-----BEGIN OPENSSH PRIVATE KEY-----\n"
            "b3BlbnNzaC1rZXktdjEAAAAABG5vbmU=\n"
            "-----END OPENSSH PRIVATE KEY-----\n";
        const QString path = writeTemp(tmp, body);
        auto info = inspectSshKey(path);
        QVERIFY(info.valid());
        QVERIFY(!info.encrypted);
    }

    void encrypted_openssh_aes256()
    {
        // OpenSSH key with cipher "aes256-ctr".
        //   "openssh-key-v1\0"                  (15 bytes)
        //   <uint32: 10> "aes256-ctr"           (4 + 10 bytes)
        // Total 29 bytes.
        // Hex: 6f70656e7373682d6b65792d76310000000000000a6165733235362d637472
        // Base64:
        QTemporaryFile tmp;
        const QByteArray body =
            "-----BEGIN OPENSSH PRIVATE KEY-----\n"
            "b3BlbnNzaC1rZXktdjEAAAAACmFlczI1Ni1jdHI=\n"
            "-----END OPENSSH PRIVATE KEY-----\n";
        const QString path = writeTemp(tmp, body);
        auto info = inspectSshKey(path);
        QVERIFY(info.valid());
        QVERIFY(info.encrypted);
    }

    void unknown_format_isValidButReportedAsUnencrypted()
    {
        // A file that exists but isn't an SSH key at all. We say
        // "valid but not encrypted" — the downstream loader will give
        // a meaningful error when it tries to actually use the key.
        QTemporaryFile tmp;
        const QString path = writeTemp(tmp, "this is not a key file\n");
        auto info = inspectSshKey(path);
        QVERIFY(info.valid());
        QVERIFY(!info.encrypted);
    }

    void truncated_openssh_handlesGracefully()
    {
        // OpenSSH-style header but body is too short to parse. Should
        // not crash; report not-encrypted.
        QTemporaryFile tmp;
        const QByteArray body =
            "-----BEGIN OPENSSH PRIVATE KEY-----\n"
            "b3Blbg==\n"  // just "open" — not enough to find cipher
            "-----END OPENSSH PRIVATE KEY-----\n";
        const QString path = writeTemp(tmp, body);
        auto info = inspectSshKey(path);
        QVERIFY(info.valid());
        QVERIFY(!info.encrypted);  // not crashed, conservative answer
    }
};

QTEST_APPLESS_MAIN(TestSshKeyInfo)
#include "test_SshKeyInfo.moc"
