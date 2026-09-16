// The handover between two launches.
//
// What crosses the pipe is a compatibility surface between two builds of this
// program that may not be the same build -- somebody updates while a copy is
// running and then double-clicks a file. So the encoding is checked on its own,
// and the refusals matter as much as the successes: a message that cannot be
// understood has to be refused rather than cast and acted on.

#include "../src/cli/instancegateway.h"

#include <QCoreApplication>
#include <QDataStream>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QIODevice>
#include <QString>
#include <QThread>
#include <QTimer>

#include <iostream>

namespace {

int failures = 0;

void check(bool condition, const QString &what)
{
    if (condition)
        return;
    std::cerr << "FAIL: " << what.toStdString() << '\n';
    ++failures;
}

CommandLine::Request sampleRequest()
{
    CommandLine::Request request;
    request.action = CommandLine::Action::Open;
    request.paths = {QStringLiteral("C:\\docs\\一.pdf"), QStringLiteral("C:\\docs\\two.pdf")};
    request.page = 41;
    return request;
}

// A payload written the way an older build might have written it, for the
// refusals below.
QByteArray payload(quint32 magic, quint32 version, qint32 action, const QStringList &paths,
                   qint32 page)
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << magic << version << action << paths << page;
    return bytes;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);

    // ---- the name of the pipe ----
    {
        const QString name = InstanceGateway::serverName();
        check(!name.isEmpty(), "there is a pipe name");
        check(name == InstanceGateway::serverName(), "and it is the same every time");
        check(name.contains(QStringLiteral("xyzpdf")), "and it says whose it is");
    }

    // ---- the round trip ----
    {
        const CommandLine::Request sent = sampleRequest();
        CommandLine::Request received;
        check(InstanceGateway::decode(InstanceGateway::encode(sent), &received),
              "a request this build encoded is understood");
        check(received.valid, "and arrives valid");
        check(received.action == sent.action, "with its action");
        check(received.paths == sent.paths, "with its paths, Chinese and all");
        // The page number is the field that would be forgotten: XYZip's
        // equivalent shipped without its preset name, and nothing caught it
        // because the forwarding path only runs when a second copy is started
        // while the first is up.
        check(received.page == 41, "and with its page number");
    }

    // ---- what has to be refused ----
    {
        CommandLine::Request received;

        check(!InstanceGateway::decode(QByteArray(), &received), "an empty message is refused");
        check(!InstanceGateway::decode(QByteArray("garbage"), &received),
              "so is one that is not even a stream");
        check(!InstanceGateway::decode(QByteArray(), nullptr),
              "and decoding into nothing does not crash");

        const QStringList paths{QStringLiteral("a.pdf")};
        check(!InstanceGateway::decode(payload(0xDEADBEEF, 1, 0, paths, -1), &received),
              "a message from something that is not us is refused");
        check(!InstanceGateway::decode(payload(0x78706466, 99, 0, paths, -1), &received),
              "and so is one from a protocol version this build does not speak");
        check(!InstanceGateway::decode(payload(0x78706466, 1, 7, paths, -1), &received),
              "and an action number that is not one of ours");

        check(InstanceGateway::decode(payload(0x78706466, 1, 1, paths, -1), &received),
              "while a print request from this build's own version is fine");
        check(received.action == CommandLine::Action::Print, "and means print");
        check(received.page == -1, "with no page");
    }

    // ---- a real handover over a real pipe ----
    //
    // The server is this thread's, and the forwarding has to happen on another:
    // forward() waits for the far end to answer, and the far end only answers
    // when this thread runs its event loop. Doing both in one thread would
    // deadlock, which is exactly the shape of the bug this arrangement is meant
    // to avoid in the program.
    {
        const QString name = InstanceGateway::serverName() + QStringLiteral("-smoke");
        InstanceGateway::Server server;
        check(server.listen(name), "the first launch becomes the instance");

        CommandLine::Request delivered;
        bool arrived = false;
        QObject::connect(&server, &InstanceGateway::Server::requestReceived,
                         [&delivered, &arrived](const CommandLine::Request &request) {
                             delivered = request;
                             arrived = true;
                         });

        bool forwarded = false;
        // QThread::create rather than a subclass: four lines instead of a class,
        // and the lambda is where the values it touches are.
        QThread *sender = QThread::create([&forwarded, name] {
            forwarded = InstanceGateway::forward(sampleRequest(), name, 10000);
        });
        sender->start();

        QElapsedTimer elapsed;
        elapsed.start();
        while (!arrived && elapsed.elapsed() < 20000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        sender->wait(10000);
        sender->deleteLater();

        check(forwarded, "the second launch handed its request over");
        check(arrived, "and the instance received it");
        check(delivered.page == 41, "with the page number intact");
        check(delivered.paths.size() == 2, "and both paths");

        // A second instance is refused: two processes both believing they are in
        // charge is what the mutex is for.
        InstanceGateway::Server second;
        check(!second.listen(name), "a second launch does not become the instance");
    }

    // ---- forwarding to nobody ----
    //
    // False rather than an error: the ordinary case for the first launch is
    // that nobody is there, and the caller then becomes the instance itself.
    {
        const bool forwarded =
            InstanceGateway::forward(sampleRequest(),
                                     InstanceGateway::serverName()
                                         + QStringLiteral("-nobody-is-here"), 500);
        check(!forwarded, "forwarding to a pipe that does not exist says so");
    }

    if (failures == 0) {
        std::cout << "the instance gateway holds together\n";
        return 0;
    }
    std::cerr << failures << " failure(s)\n";
    return 1;
}
