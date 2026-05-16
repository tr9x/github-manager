#pragma once

// HostKeyApprover — bridge between libgit2's certificate_check
// callback (running on the worker thread) and the GUI thread that
// owns the modal HostKeyApprovalDialog.
//
// Threading is the whole point of this class:
//   * It lives in the GUI thread (owned by MainWindow).
//   * libgit2 calls a free function on the worker thread, which uses
//     QMetaObject::invokeMethod with Qt::BlockingQueuedConnection
//     to call requestApproval() on this object.
//   * The slot runs on the GUI thread, pops the modal dialog, waits
//     for user input, returns a bool.
//   * The worker thread unblocks with the answer in hand.
//
// Why BlockingQueuedConnection is OK here despite the general warning:
//   * The blocking interval is bounded by user click latency — humans
//     click within ~30s, which is acceptable for a clone operation
//     that's already a long-running async task.
//   * There's no reverse dependency: the GUI thread doesn't wait for
//     anything from the worker while showing the dialog. No cyclical
//     wait, so no deadlock.
//   * We use a singleton-style instance owned by MainWindow, accessed
//     via a global pointer set during construction. Worker callbacks
//     pick it up safely.
//
// Known hosts persistence:
//   * On accept, we append a line to ~/.ssh/known_hosts using the
//     hashed-hostname format ssh itself writes (HashKnownHosts=yes).
//   * On cancel, the file isn't touched; next clone attempt to the
//     same host will prompt again.

#include <QObject>
#include <QString>

class QWidget;

namespace ghm::ui {

class HostKeyApprover : public QObject {
    Q_OBJECT
public:
    explicit HostKeyApprover(QWidget* dialogParent, QObject* parent = nullptr);
    ~HostKeyApprover() override;

    // Worker-thread side calls this. Pops the dialog on the GUI thread
    // (we are AffinityToGui because we live there) and returns
    // user choice. Must be Q_INVOKABLE for invokeMethod string-based
    // dispatch.
    //
    // If the user accepts, this also writes the entry to known_hosts.
    // Both inputs are pre-formatted: host as "github.com" or
    // "host.example.com:22", fingerprint as a SHA256 in base64.
    Q_INVOKABLE bool requestApproval(const QString& host,
                                     const QString& fingerprint,
                                     const QString& keyType,
                                     const QString& rawKeyBase64);

    // Globally-accessible singleton pointer. Set in constructor,
    // cleared in destructor. Used by the libgit2 certificate_check
    // callback (a free function) to find this object from the worker
    // thread. Safe because:
    //   * MainWindow constructs the approver during init and only
    //     destructs it on shutdown — no in-flight worker callbacks
    //     can outlive it (worker is joined first).
    //   * The pointer is read-only after init; no race on read.
    static HostKeyApprover* instance();

private:
    QWidget* dialogParent_;
};

} // namespace ghm::ui
