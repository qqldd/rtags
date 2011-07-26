#include <QCoreApplication>
#include <QString>
#include <stdio.h>
#include "Daemon.h"
#include "Client.h"
#include "Utils.h"
#include <syslog.h>

#define CLIENT_CONNECT_ATTEMPTS 5
#define CLIENT_CONNECT_DELAY 1

void syslogMsgHandler(QtMsgType t, const char* str)
{
    int priority = LOG_WARNING;
    static const char *names[] = { "DEBUG", "WARNING", "CRITICAL", "FATAL" };
    switch (t) {
    case QtDebugMsg:
        priority = LOG_DEBUG;
        break;
    case QtWarningMsg:
        priority = LOG_WARNING;
        break;
    case QtCriticalMsg:
        priority = LOG_CRIT;
        break;
    case QtFatalMsg:
        priority = LOG_CRIT;
        break;
    }
    fprintf(stderr, "%s (%s)\n", str, names[t]);
    QFile file("/tmp/rtags.log");
    file.open(QIODevice::WriteOnly|QIODevice::Append);
    char buf[1024];
    const int s = snprintf(buf, 1023, "%s (%s)\n", str, names[t]);
    file.write(buf, s);
    syslog(priority, "%s (%s)\n", str, names[t]);
}

class ArgParser
{
public:
    ArgParser(int argc, char** argv);

    bool isValid() const;

    QVariantMap arguments() const;

private:
    bool parse(int argc, char** argv);
    void addValue(const QString& key, const QString& value);

private:
    bool m_valid;
    QVariantMap m_args;
};

ArgParser::ArgParser(int argc, char **argv)
{
    m_valid = parse(argc, argv);
}

bool ArgParser::isValid() const
{
    return m_valid;
}

QVariantMap ArgParser::arguments() const
{
    return m_args;
}

void ArgParser::addValue(const QString &key, const QString &value)
{
    bool ok;
    int intvalue = value.toInt(&ok);
    if (ok) {
        m_args[key] = intvalue;
        return;
    }
    double doublevalue = value.toDouble(&ok);
    if (ok) {
        m_args[key] = doublevalue;
        return;
    }
    m_args[key] = value;
}

bool ArgParser::parse(int argc, char **argv)
{
    m_args.clear();

    QString current;
    const char** end = const_cast<const char**>(argv + argc);
    for (; argv != end; ++argv) {
        current = QLatin1String(*argv);
        if (current.startsWith(QLatin1Char('-'))) {
            const int eqpos = current.indexOf(QLatin1Char('='));
            if (eqpos == -1) { // take next argument
                ++argv;
                if (argv == end)
                    return false;
                while (!current.isEmpty() && current.at(0) == QLatin1Char('-'))
                    current = current.mid(1);
                QString value = QLatin1String(*argv);
                if (current.isEmpty() || value.isEmpty())
                    return false;
                addValue(current, value);
            } else { // use everything past '='
                QString value = current.mid(eqpos + 1);
                current = current.left(eqpos);
                while (!current.isEmpty() && current.at(0) == QLatin1Char('-'))
                    current = current.mid(1);
                if (value.isEmpty() || current.isEmpty())
                    return false;
                addValue(current, value);
            }
        } else { // doesn't start with a '-', ignore for now
        }
    }
    return true;
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QThread::currentThread()->setObjectName("main");
    ArgParser args(argc, argv);
    QVariantMap argsmap = args.arguments();
    if (argsmap.contains("--verbose")) {
        Options::s_verbose = true;
        argsmap.remove("--verbose");
    }
    if (argsmap.contains("-v")) {
        argsmap.remove("-v");
        Options::s_verbose = true;
    }

    if (argsmap.isEmpty())
        argsmap.insert(QLatin1String("command"), QLatin1String("syntax"));

    QString cmd = argsmap.value(QLatin1String("command")).toString();
    FUNC;
    QCoreApplication::setOrganizationDomain("www.rtags.com");
    QCoreApplication::setOrganizationName("RTags");
    QCoreApplication::setApplicationName("rtags");


    if (cmd == QLatin1String("daemonize")) {
        Daemon daemon;
        qInstallMsgHandler(syslogMsgHandler);
        if (daemon.start())
            return app.exec();
        else
            return -2;
    } else {
        Client client;
        if (!client.connect()) {
            if (cmd == QLatin1String("quit"))
                return 0;
            client.startDaemon(app.arguments());
            for (int i = 0; i < CLIENT_CONNECT_ATTEMPTS; ++i) {
                if (client.connect()) {
                    break;
                }
                sleep(CLIENT_CONNECT_DELAY);
            }
        }
        for (int i = 0; i < CLIENT_CONNECT_ATTEMPTS; ++i) {
            if (client.connected()) {
                QVariantMap replymap = client.exec(argsmap);
                QString reply = replymap.value(QLatin1String("result")).toString();
                if (!reply.isEmpty())
                    printf("%s\n", qPrintable(reply));
                return 0;
            }
            sleep(CLIENT_CONNECT_DELAY);
        }
    }
    qWarning("Couldn't connect to daemon");

    return -1;
}
