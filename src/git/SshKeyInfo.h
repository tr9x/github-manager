#pragma once

// SshKeyInfo — inspect an SSH private key file *offline* (no libssh2,
// no libgit2 calls, no agent contact) to decide what we need from
// the user before kicking off an authentication attempt.
//
// We use this for one purpose: deciding whether to prompt the user
// for a passphrase BEFORE the worker thread tries to use the key.
// libgit2's credential callback runs in the worker thread, where
// popping up a modal dialog is fragile (Qt warnings or worse). By
// inspecting the file ahead of time on the GUI thread, we can:
//
//   * Prompt only when the key is actually encrypted
//   * Avoid prompting for a passphrase the user doesn't have
//
// The check is intentionally cheap: we read the header bytes and
// pattern-match. We don't validate the key, parse the body, or call
// crypto. If the file is malformed, we say "not encrypted" — the
// real load attempt downstream will fail with a meaningful error
// either way.
//
// Supported formats:
//   * OpenSSH (the default since OpenSSH 7.8): kdf field in the
//     binary container tells us. We check the human-readable header
//     and the kdfname token for "bcrypt" or "aes256-ctr".
//   * RSA / DSA / EC PEM (legacy): the "Proc-Type: 4,ENCRYPTED"
//     header inside the PEM block.
//
// Edge cases:
//   * Non-existent file → returns invalid result (isValid=false)
//   * Permission denied → invalid
//   * Unrecognised format → exists=true, encrypted=false (let the
//     downstream loader handle the real diagnosis)

#include <QString>
#include <QFile>
#include <QFileInfo>

namespace ghm::git {

struct SshKeyInfo {
    bool exists{false};
    bool isReadable{false};
    bool encrypted{false};

    // True when we definitively answered the encrypted/not question.
    // False when the file isn't readable or the format is unknown —
    // caller should NOT prompt for passphrase based on this struct
    // when valid is false.
    bool valid() const { return exists && isReadable; }
};

inline SshKeyInfo inspectSshKey(const QString& path)
{
    SshKeyInfo info;
    QFileInfo fi(path);
    info.exists = fi.exists() && fi.isFile();
    if (!info.exists) return info;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return info;
    info.isReadable = true;

    // We only need the first few KB — encryption markers are always
    // in the file header. Cap at 8KB to avoid loading huge unrelated
    // files into memory if the user picks the wrong path.
    const QByteArray head = f.read(8192);

    // PEM-style "Proc-Type: 4,ENCRYPTED" — used by classic RSA/DSA/EC
    // keys generated before OpenSSH 7.8.
    if (head.contains("Proc-Type: 4,ENCRYPTED")) {
        info.encrypted = true;
        return info;
    }

    // OpenSSH key format. The container is base64 inside
    // -----BEGIN OPENSSH PRIVATE KEY----- ... -----END...-----
    // We don't decode the base64; instead we check the human-readable
    // outer block and the kdfname embedded near the start of the
    // base64 payload. When the kdfname is "bcrypt" or "aes256-ctr",
    // the key is encrypted; when it's "none", it isn't.
    if (head.contains("BEGIN OPENSSH PRIVATE KEY")) {
        // Strip header line and decode just enough of the base64 to
        // see the kdfname token. The first bytes of the decoded
        // payload (after the "openssh-key-v1\0" magic) carry a
        // length-prefixed kdfname string.
        const int begin = head.indexOf("-----BEGIN OPENSSH PRIVATE KEY-----");
        if (begin < 0) return info;
        const int dataStart = head.indexOf('\n', begin) + 1;
        const int dataEnd = head.indexOf("-----END", dataStart);
        if (dataEnd <= dataStart) return info;
        QByteArray b64 = head.mid(dataStart, dataEnd - dataStart);
        b64.replace('\n', "").replace('\r', "").replace(' ', "");
        QByteArray decoded = QByteArray::fromBase64(b64);
        // Magic + null
        const char magic[] = "openssh-key-v1";
        if (!decoded.startsWith(QByteArray(magic, sizeof(magic) - 1))) return info;
        // skip magic + null terminator (15 bytes total)
        const int afterMagic = 15;
        if (decoded.size() < afterMagic + 8) return info;
        // ciphername (length-prefixed). uint32 big-endian length.
        auto readU32 = [](const QByteArray& b, int off) -> int {
            if (off + 4 > b.size()) return -1;
            return (static_cast<unsigned char>(b[off])   << 24) |
                   (static_cast<unsigned char>(b[off+1]) << 16) |
                   (static_cast<unsigned char>(b[off+2]) <<  8) |
                   (static_cast<unsigned char>(b[off+3]));
        };
        const int cipherLen = readU32(decoded, afterMagic);
        if (cipherLen < 0 || cipherLen > 64) return info;
        const QByteArray cipherName =
            decoded.mid(afterMagic + 4, cipherLen);
        // "none" → unencrypted; anything else (aes256-ctr, etc.) → encrypted.
        info.encrypted = (cipherName != "none");
        return info;
    }

    // Unknown format — be conservative, say "not encrypted" so we
    // don't unnecessarily prompt. The downstream loader will give a
    // meaningful error if it really fails.
    return info;
}

} // namespace ghm::git
