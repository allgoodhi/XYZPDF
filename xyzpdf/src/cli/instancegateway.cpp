#include "instancegateway.h"

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <QCryptographicHash>
#include <QDataStream>
#include <QIODevice>
#include <QLocalServer>
#include <QLocalSocket>

#include <string>

namespace InstanceGateway {
namespace {

// Bumped when what crosses the pipe changes shape. A running instance from an
// older build would otherwise read the new bytes as whatever the old fields
// happened to line up with, and act on it.
constexpr quint32 kProtocolVersion = 1;

constexpr quint32 kMagic = 0x78706466; // "xpdf"

} // namespace

QString serverName()
{
    // The user name is hashed rather than used directly: it can contain
    // anything a person's account is called, and a pipe name has its own rules
    // about what a name may hold.
    const QByteArray user = qgetenv("USERNAME") + '\\' + qgetenv("USERDOMAIN");
    const QByteArray digest =
        QCryptographicHash::hash(user, QCryptographicHash::Sha256).left(8).toHex();
    return QStringLiteral("xyzpdf-instance-") + QString::fromLatin1(digest);
}

QByteArray encode(const CommandLine::Request &request)
{
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    // Everything the request needs, not just the paths. XYZip's equivalent
    // forgot the preset name once, so "compress with this preset" forwarded to
    // an instance already running arrived with no preset and compressed with
    // the defaults -- and nothing caught it, because the forwarding path only
    // runs when a second copy is started while the first is up. The page number
    // is here for exactly that reason.
    stream << kMagic << kProtocolVersion << static_cast<qint32>(request.action) << request.paths
           << static_cast<qint32>(request.page);
    return payload;
}

bool decode(const QByteArray &payload, CommandLine::Request *request)
{
    if (!request)
        return false;

    QDataStream stream(payload);
    stream.setVersion(QDataStream::Qt_6_0);

    quint32 magic = 0;
    quint32 version = 0;
    qint32 action = 0;
    QStringList paths;
    qint32 page = -1;
    stream >> magic >> version >> action >> paths >> page;
    if (stream.status() != QDataStream::Ok)
        return false;
    if (magic != kMagic || version != kProtocolVersion)
        return false;

    // The action is an index into an enum this build knows. A number from a
    // different build is not something to cast and hope about.
    if (!CommandLine::isKnownAction(action))
        return false;

    request->action = static_cast<CommandLine::Action>(action);
    request->paths = paths;
    request->page = page < -1 ? -1 : page;
    request->valid = true;
    request->errorMessage.clear();
    return true;
}

bool forward(const CommandLine::Request &request, const QString &name, int timeoutMilliseconds)
{
    QLocalSocket socket;
    socket.connectToServer(name);
    if (!socket.waitForConnected(timeoutMilliseconds))
        return false;

    // Length first, so the far end knows when the message is complete without
    // waiting to see whether more is coming.
    QByteArray frame;
    {
        const QByteArray payload = encode(request);
        QDataStream stream(&frame, QIODevice::WriteOnly);
        stream.setVersion(QDataStream::Qt_6_0);
        stream << static_cast<quint32>(payload.size());
        frame += payload;
    }
    if (socket.write(frame) != frame.size())
        return false;
    socket.flush();

    // The far end closing is the acknowledgement.
    //
    // Not waitForBytesWritten: bytes handed to a pipe are not bytes anybody
    // acted on, and it reports false when the write already completed -- there
    // is nothing pending, so nothing happens, so it says nothing happened.
    // Every message that went out cleanly then looks like one that did not, and
    // this launch decides nobody was listening and does the work itself.
    //
    // The server reads the whole message and then closes, so waiting for the
    // close is both simpler and a stronger promise.
    if (socket.state() == QLocalSocket::UnconnectedState)
        return true;
    return socket.waitForDisconnected(timeoutMilliseconds);
}

Server::Server(QObject *parent) : QObject(parent), server(new QLocalServer(this))
{
    connect(server, &QLocalServer::newConnection, this, &Server::acceptConnection);
}

Server::~Server()
{
#ifdef Q_OS_WIN
    if (primaryLock)
        CloseHandle(static_cast<HANDLE>(primaryLock));
#endif
}

bool Server::listen(const QString &name)
{
    // Who is the instance is decided by a named mutex, not by the pipe.
    //
    // QLocalServer::listen does not fail on a name that is already in use here:
    // Windows lets a second CreateNamedPipe add another instance of the same
    // pipe rather than refusing it. Both processes would then believe they are
    // the one in charge, and which of them a later launch reaches would be
    // whichever pipe instance happened to be free. A mutex answers the question
    // once, atomically, which is what it is for.
#ifdef Q_OS_WIN
    const std::wstring lockName =
        (QStringLiteral("Local\\") + name + QStringLiteral("-primary")).toStdWString();
    HANDLE lock = CreateMutexW(nullptr, TRUE, lockName.c_str());
    if (!lock)
        return false;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(lock);
        return false;
    }
    primaryLock = lock;
#endif

    if (server->listen(name))
        return true;

#ifdef Q_OS_WIN
    if (primaryLock) {
        CloseHandle(static_cast<HANDLE>(primaryLock));
        primaryLock = nullptr;
    }
#endif
    return false;
}

void Server::acceptConnection()
{
    while (QLocalSocket *socket = server->nextPendingConnection()) {
        // Read as the bytes turn up rather than waiting for them.
        //
        // Waiting here blocked this thread -- the one drawing the window -- for
        // as long as it took, and the far end was meanwhile waiting for this
        // end to answer. With several launches handing work over at once that
        // was a stall each, in the middle of everything else.
        partial.insert(socket, QByteArray());
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] { readFrom(socket); });
        connect(socket, &QLocalSocket::disconnected, this, [this, socket] {
            partial.remove(socket);
            socket->deleteLater();
        });
        readFrom(socket);
    }
}

void Server::readFrom(QLocalSocket *socket)
{
    auto buffered = partial.find(socket);
    if (buffered == partial.end())
        return;
    buffered->append(socket->readAll());

    constexpr int kLengthBytes = 4;
    if (buffered->size() < kLengthBytes)
        return;

    quint32 length = 0;
    {
        QDataStream stream(*buffered);
        stream.setVersion(QDataStream::Qt_6_0);
        stream >> length;
    }
    // A length nobody could mean. Refusing beats allocating whatever a stray
    // connection asked for.
    constexpr quint32 kSaneLimit = 1u << 20;
    if (length > kSaneLimit) {
        socket->disconnectFromServer();
        return;
    }
    if (static_cast<quint32>(buffered->size()) < kLengthBytes + length)
        return;

    CommandLine::Request request;
    if (decode(buffered->mid(kLengthBytes, static_cast<int>(length)), &request))
        pending.append(request);

    // Closing is how the far end learns the message arrived, so it happens
    // whether or not the message made sense -- a sender left hanging would give
    // up and do the work itself.
    socket->disconnectFromServer();
    deliverPending();
}

void Server::deliverPending()
{
    if (delivering)
        return;
    delivering = true;
    // Re-checked after every handler, because handling one request opens a
    // dialog and that is exactly when the next arrives.
    while (!pending.isEmpty())
        Q_EMIT requestReceived(pending.takeFirst());
    delivering = false;
}

} // namespace InstanceGateway
