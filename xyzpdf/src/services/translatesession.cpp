#include "translatesession.h"

#include "../core/appsettings.h"
#include "../core/windowdialogs.h"
#include "../ui/translatedialog.h"

#include <QCoreApplication>
#include <QFutureWatcher>
#include <QMessageBox>
#include <QPushButton>
#include <QtConcurrent/QtConcurrentRun>

TranslateSession::TranslateSession(QWidget *host)
    : QObject(host)
    , host(host)
{
}

TranslateSession::~TranslateSession() = default;

void TranslateSession::setKeyStoreForTesting(std::unique_ptr<Translate::KeyStore> store)
{
    translateStore = std::move(store);
}

void TranslateSession::translateText(const QString &raw)
{
    const QString text = raw.trimmed();
    if (text.isEmpty())
        return; // the action is disabled without text; a shortcut is not

    // Everything the request needs is settled here, on the thread that owns the
    // window: which service, whether it has a key, and whether the reader has
    // agreed to talk to it. The worker gets values, not this object.
    const Translate::Provider provider =
        Translate::providerFromId(AppSettings::translateProvider());
    const QString providerId = Translate::providerId(provider);

    QString key;
    QString storeError;
    if (provider != Translate::Provider::None)
        keyStore()->read(providerId, &key, &storeError);
    if (!storeError.isEmpty()) {
        // A store that fails is not the same as "no key configured": saying
        // "输入密钥" while the credential manager is refusing would send the
        // reader to a field that cannot work either.
        WindowDialogs::warning(host->window(), QCoreApplication::translate("DocumentTab", "翻译"),
                               QCoreApplication::translate("DocumentTab", "读取 API 密钥失败：%1")
                                   .arg(storeError));
        return;
    }
    key = key.trimmed();

    switch (Translate::requestGate(provider, key, AppSettings::translateConsentGiven(providerId))) {
    case Translate::Gate::NotConfigured:
        askForSetup();
        return;
    case Translate::Gate::NeedsConsent:
        if (!askForConsent(provider, key))
            return;
        AppSettings::setTranslateConsent(providerId, true);
        break;
    case Translate::Gate::Ready:
        break;
    }

    // Shown before the request goes out, so the reader sees the words they asked
    // about immediately and a "translating…" line while the round trip happens --
    // the request itself runs off this thread, below.
    auto *dialog = new TranslateDialog(host);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setPending(text);
    dialog->show();
    start(text, provider, key, dialog);
}

void TranslateSession::askForSetup()
{
    QMessageBox box(host->window());
    box.setIcon(QMessageBox::Information);
    box.setWindowTitle(QCoreApplication::translate("DocumentTab", "翻译"));
    box.setText(QCoreApplication::translate("DocumentTab", "翻译需要一个服务的 API 密钥。"));
    box.setInformativeText(
        QCoreApplication::translate("DocumentTab", "请先在设置里选择翻译服务并填写密钥。"));
    QPushButton *settings =
        box.addButton(QCoreApplication::translate("DocumentTab", "去设置"), QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() == settings)
        Q_EMIT setupRequested();
}

bool TranslateSession::askForConsent(Translate::Provider provider, const QString &key)
{
    // The service's name and the host its key actually uses, because "a
    // translation service" is not something a reader can decide about. DeepL's
    // free and paid tiers answer on different hosts, and the suffix on the key is
    // the only thing that says which one the words will reach -- so the box says
    // it before the first request rather than leaving it in a log afterwards.
    QMessageBox box(host->window());
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(QCoreApplication::translate("DocumentTab", "翻译"));
    box.setText(QCoreApplication::translate("DocumentTab", "选中的文字会发送给 %1（%2）。")
                    .arg(Translate::providerName(provider),
                         Translate::endpointHost(provider, key)));
    box.setInformativeText(QCoreApplication::translate("DocumentTab",
                                                      "只发送选中的文字，不发送文档中的其他内容。"));
    QPushButton *send =
        box.addButton(QCoreApplication::translate("DocumentTab", "发送"), QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel);
    box.exec();
    return box.clickedButton() == send;
}

Translate::KeyStore *TranslateSession::keyStore()
{
    if (!translateStore)
        translateStore = Translate::makeSystemKeyStore();
    return translateStore.get();
}

void TranslateSession::start(const QString &text, Translate::Provider provider, const QString &key,
                             QPointer<TranslateDialog> dialog)
{
    // The same shape as a recognition: the text is copied into the worker, the
    // window is watched with a QPointer so a result nobody is waiting for is
    // dropped, and nothing in the task touches this object or the document -- so
    // a reload under a running translation cannot crash the way a render can.
    auto *watcher = new QFutureWatcher<Translate::Result>(this);
    connect(watcher, &QFutureWatcher<Translate::Result>::finished, this, [watcher, dialog] {
        const Translate::Result result = watcher->result();
        watcher->deleteLater();
        if (dialog)
            dialog->setResult(result);
    });
    watcher->setFuture(QtConcurrent::run([text, provider, key] {
        const std::unique_ptr<Translate::Translator> translator =
            Translate::makeTranslator(provider, key);
        return translator->translate(text);
    }));
}
