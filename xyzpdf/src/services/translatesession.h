// The translation of one piece of text: the two questions that must be answered
// before a byte leaves the machine, the key it needs, the request, and the window
// the answer arrives in.
//
// Cut out of DocumentTab (docs/review-03.md §1.2) because it is a job that takes
// the reader's words away and comes back later -- and because the consent rule is
// one place rather than a condition threaded through the translation path.
//
// Before any byte leaves the machine this checks the two things the reader must
// have done: chosen a service with a key, and agreed to send selected text to it.
// Neither is a silent default -- an unconfigured program says so and offers the
// settings page, and a configured service is asked about once per provider. See
// Translate::requestGate.
//
// The key comes from a store, and in a test from an in-memory one: the program
// installs nothing there and uses the Windows credential manager, so an automated
// run can neither read a real key nor write to the machine it runs on (see
// src/translate/keystore.h). That seam lives here now, and the tab keeps the
// forwarding call the tests use.
//
// Unlike the other services, this one is not a job drainRenders() waits for: the
// request carries the text by value and touches no document, so a reload under a
// running translation cannot crash the way a render can.
#pragma once

#include "../translate/keystore.h"
#include "../translate/translator.h"

#include <QObject>
#include <QPointer>
#include <QString>

#include <memory>

class TranslateDialog;
class QWidget;

class TranslateSession : public QObject
{
    Q_OBJECT

public:
    // `host` is the tab: the parent for the window an answer arrives in, and the
    // window the two questions are asked against.
    explicit TranslateSession(QWidget *host);
    ~TranslateSession() override;

    // Test seam only: the store this reads its key from.
    void setKeyStoreForTesting(std::unique_ptr<Translate::KeyStore> store);

    // Translate an explicit piece of text -- the words just recognized from a
    // box, or a selection. Empty text does nothing: the action that reaches it is
    // disabled then anyway.
    void translateText(const QString &text);

Q_SIGNALS:
    // The reader asked to be taken to the translation settings -- the 「去设置」
    // button in the box that appears when translation has no service or key yet.
    // The window owns the settings dialog, so this asks rather than opening one.
    void setupRequested();

private:
    // The two boxes translateText can raise before a request: one explains that a
    // key is needed and offers the settings page, the other asks whether the
    // selected text may be sent to this service.
    void askForSetup();
    bool askForConsent(Translate::Provider provider, const QString &key);

    // Created on first use: a tab that never translates never touches the
    // credential manager.
    Translate::KeyStore *keyStore();

    // The same shape as a recognition: the request runs off the UI thread (a round
    // trip is a second or more), and the window is watched so a result nobody is
    // waiting for is dropped. `text` is copied into the worker, and so are the
    // service and key: the consent check belongs on the thread with the window,
    // and the worker must not reach into settings or the credential manager.
    void start(const QString &text, Translate::Provider provider, const QString &key,
               QPointer<TranslateDialog> dialog);

    QWidget *host = nullptr;
    // The key store translation reads from. Null until the first translation,
    // then the Windows credential manager -- or, in a test, the in-memory store
    // handed in through setKeyStoreForTesting().
    std::unique_ptr<Translate::KeyStore> translateStore;
};
