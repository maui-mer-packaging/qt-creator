/**************************************************************************
**
** This file is part of Qt Creator
**
** Copyright (c) 2009 Nokia Corporation and/or its subsidiary(-ies).
**
** Contact:  Qt Software Information (qt-info@nokia.com)
**
** Commercial Usage
**
** Licensees holding valid Qt Commercial licenses may use this file in
** accordance with the Qt Commercial License Agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Nokia.
**
** GNU Lesser General Public License Usage
**
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** If you are unsure which license is appropriate for your use, please
** contact the sales department at qt-sales@nokia.com.
**
**************************************************************************/

#include "consoleprocess.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QTemporaryFile>

#include <QtNetwork/QLocalSocket>

#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

using namespace Core::Utils;

ConsoleProcess::ConsoleProcess(QObject *parent)
    : QObject(parent)
{
    m_debug = false;
    m_appPid = 0;
    m_stubSocket = 0;

    connect(&m_stubServer, SIGNAL(newConnection()), SLOT(stubConnectionAvailable()));

    m_process.setProcessChannelMode(QProcess::ForwardedChannels);
    connect(&m_process, SIGNAL(finished(int, QProcess::ExitStatus)),
            SLOT(stubExited()));
}

ConsoleProcess::~ConsoleProcess()
{
    stop();
}

bool ConsoleProcess::start(const QString &program, const QStringList &args)
{
    if (isRunning())
        return false;

    QString err = stubServerListen();
    if (!err.isEmpty()) {
        emit processError(tr("Cannot set up comm channel: %1").arg(err));
        return false;
    }

    QStringList xtermArgs;
    xtermArgs << "-e"
#ifdef Q_OS_MAC
              << (QCoreApplication::applicationDirPath() + "/../Resources/qtcreator_process_stub")
#else
              << (QCoreApplication::applicationDirPath() + "/qtcreator_process_stub")
#endif
              << (m_debug ? "debug" : "exec")
              << m_stubServer.fullServerName()
              << tr("Press <RETURN> to close this window...")
              << workingDirectory() << environment() << ""
              << program << args;

    m_process.start(QLatin1String("xterm"), xtermArgs);
    if (!m_process.waitForStarted()) {
        stubServerShutdown();
        emit processError(tr("Cannot start console emulator xterm."));
        return false;
    }
    m_executable = program;
    emit wrapperStarted();
    return true;
}

void ConsoleProcess::stop()
{
    if (!isRunning())
        return;
    stubServerShutdown();
    m_appPid = 0;
    m_process.terminate();
    if (!m_process.waitForFinished(1000))
        m_process.kill();
    m_process.waitForFinished();
}

bool ConsoleProcess::isRunning() const
{
    return m_process.state() != QProcess::NotRunning;
}

QString ConsoleProcess::stubServerListen()
{
    // We need to put the socket in a private directory, as some systems simply do not
    // check the file permissions of sockets.
    QString stubFifoDir;
    forever {
        {
            QTemporaryFile tf;
            if (!tf.open())
                return tr("Cannot create temporary file: %2").arg(tf.errorString());
            stubFifoDir = QFile::encodeName(tf.fileName());
        }
        // By now the temp file was deleted again
        m_stubServerDir = QFile::encodeName(stubFifoDir);
        if (!::mkdir(m_stubServerDir.constData(), 0700))
            break;
        if (errno != EEXIST)
            return tr("Cannot create temporary directory %1: %2").arg(stubFifoDir, strerror(errno));
    }
    QString stubServer  = stubFifoDir + "/stub-socket";
    if (!m_stubServer.listen(stubServer)) {
        ::rmdir(m_stubServerDir.constData());
        return tr("Cannot create socket %1: %2").arg(stubServer, m_stubServer.errorString());
    }
    return QString();
}

void ConsoleProcess::stubServerShutdown()
{
    delete m_stubSocket;
    m_stubSocket = 0;
    if (m_stubServer.isListening()) {
        m_stubServer.close();
        ::rmdir(m_stubServerDir.constData());
    }
}

void ConsoleProcess::stubConnectionAvailable()
{
    m_stubSocket = m_stubServer.nextPendingConnection();
    connect(m_stubSocket, SIGNAL(readyRead()), SLOT(readStubOutput()));
}

static QString errorMsg(int code)
{
    return QString::fromLocal8Bit(strerror(code));
}

void ConsoleProcess::readStubOutput()
{
    while (m_stubSocket->canReadLine()) {
        QByteArray out = m_stubSocket->readLine();
        out.chop(1); // \n
        if (out.startsWith("err:chdir ")) {
            emit processError(tr("Cannot change to working directory %1: %2")
                              .arg(workingDirectory(), errorMsg(out.mid(10).toInt())));
        } else if (out.startsWith("err:exec ")) {
            emit processError(tr("Cannot execute %1: %2")
                              .arg(m_executable, errorMsg(out.mid(9).toInt())));
        } else if (out.startsWith("pid ")) {
            m_appPid = out.mid(4).toInt();
            emit processStarted();
        } else if (out.startsWith("exit ")) {
            m_appStatus = QProcess::NormalExit;
            m_appCode = out.mid(5).toInt();
            m_appPid = 0;
            emit processStopped();
        } else if (out.startsWith("crash ")) {
            m_appStatus = QProcess::CrashExit;
            m_appCode = out.mid(6).toInt();
            m_appPid = 0;
            emit processStopped();
        } else {
            emit processError(tr("Unexpected output from helper program."));
            m_process.terminate();
            break;
        }
    }
}

void ConsoleProcess::stubExited()
{
    // The stub exit might get noticed before we read the error status.
    if (m_stubSocket && m_stubSocket->state() == QLocalSocket::ConnectedState)
        m_stubSocket->waitForDisconnected();
    stubServerShutdown();
    if (m_appPid) {
        m_appStatus = QProcess::CrashExit;
        m_appCode = -1;
        m_appPid = 0;
        emit processStopped(); // Maybe it actually did not, but keep state consistent
    }
    emit wrapperStopped();
}
