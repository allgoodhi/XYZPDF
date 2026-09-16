#ifndef INSTANCEGATEWAY_H
#define INSTANCEGATEWAY_H

#include "commandline.h"

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

class QLocalServer;
class QLocalSocket;

// How a second launch hands its work to the first.
//
// Explorer starts one process per selected item, so opening ten documents from
// a multi-selection starts ten copies of xyzpdf -- ten windows, ten sets of
// threads, and the same file read twice if two of them are the same file. What
// the user meant was ten tabs.
//
// The transport is a named pipe. Everything here is kept apart from the window
// on purpose: what crosses the pipe is a compatibility surface between two
// builds of xyzpdf that may not be the same build, and it is worth testing on
// its own.
namespace InstanceGateway {

// The name a second launch connects to.
//
// Per user: a named pipe is visible to the whole machine, and two people signed
// in at once must not end up driving each other's xyzpdf.
QString serverName();

// What crosses the pipe.
QByteArray encode(const CommandLine::Request &request);
bool decode(const QByteArray &payload, CommandLine::Request *request);

// Hands `request` to an instance that is already listening on `name`.
//
// False means nobody was there -- which is the ordinary case for the first
// launch, not a failure. The caller then becomes the instance itself.
bool forward(const CommandLine::Request &request, const QString &name,
             int timeoutMilliseconds = 1000);

// The instance others forward to.
class Server : public QObject
{
    Q_OBJECT

public:
    explicit Server(QObject *parent = nullptr);
    ~Server() override;

    // False when somebody else is already the instance, which is the race two
    // launches starting at the same moment can lose.
    bool listen(const QString &name);

Q_SIGNALS:
    // One at a time, in the order they arrived.
    //
    // Handling a request opens a tab and reads a document, and reading a
    // password-protected one puts up a dialog -- which spins a nested event
    // loop, which is where the next request arrives. Delivering it there would
    // re-enter an open that is already running; anything that turns up
    // mid-flight waits its turn instead.
    void requestReceived(const CommandLine::Request &request);

private:
    void acceptConnection();
    void readFrom(QLocalSocket *socket);
    void deliverPending();

    QLocalServer *server = nullptr;
    // Held for as long as this process is the instance. A HANDLE, kept as void*
    // so the header does not drag in windows.h.
    void *primaryLock = nullptr;
    // What has arrived so far on each connection. A message is not a packet:
    // it can turn up in pieces, and two of them can turn up at once.
    QHash<QLocalSocket *, QByteArray> partial;
    QList<CommandLine::Request> pending;
    bool delivering = false;
};

} // namespace InstanceGateway

#endif // INSTANCEGATEWAY_H
