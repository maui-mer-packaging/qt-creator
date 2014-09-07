/****************************************************************************
**
** Copyright (C) 2014 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://qt.digia.com/licensing.  For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
****************************************************************************/

#include "winrtdebugsupport.h"
#include "winrtrunconfiguration.h"
#include "winrtrunnerhelper.h"

#include <debugger/debuggerengine.h>
#include <debugger/debuggerkitinformation.h>
#include <debugger/debuggerplugin.h>
#include <debugger/debuggerrunner.h>
#include <debugger/debuggerstartparameters.h>
#include <projectexplorer/target.h>
#include <projectexplorer/toolchain.h>

#include <QFileInfo>
#include <QLocalServer>
#include <QLocalSocket>

#include <utils/qtcprocess.h>

namespace WinRt {
namespace Internal {

using namespace ProjectExplorer;

WinRtDebugSupport::WinRtDebugSupport(RunControl *runControl, WinRtRunnerHelper *runner)
    : QObject(runControl)
    , m_debugRunControl(runControl)
    , m_runner(runner)
{
    connect(m_debugRunControl, SIGNAL(finished()), this, SLOT(finish()));
}

void WinRtDebugSupport::finish()
{
    m_runner->stop();
}

RunControl *WinRtDebugSupport::createDebugRunControl(WinRtRunConfiguration *runConfig,
                                                     RunMode mode,
                                                     QString *errorMessage)
{
    // FIXME: This is just working for local debugging;
    using namespace Debugger;
    DebuggerStartParameters params;
    params.startMode = AttachExternal;
    params.languages |= CppLanguage;
    params.breakOnMain = mode == DebugRunModeWithBreakOnMain;
    // The first Thread needs to be resumed manually.
    params.commandsAfterConnect = "~0 m";
    Kit *kit = runConfig->target()->kit();
    params.debuggerCommand = DebuggerKitInformation::debuggerCommand(kit).toString();
    if (ToolChain *tc = ToolChainKitInformation::toolChain(kit))
        params.toolChainAbi = tc->targetAbi();

    QFileInfo debuggerHelper(QCoreApplication::applicationDirPath()
                             + QLatin1String("/winrtdebughelper.exe"));
    if (!debuggerHelper.isExecutable()) {
        *errorMessage = tr("The WinRT debugging helper is missing from your Qt Creator "
                           "installation. It was assumed to be located at %1").arg(
                    debuggerHelper.absoluteFilePath());
        return 0;
    }

    WinRtRunnerHelper *runner = new WinRtRunnerHelper(runConfig, errorMessage);
    if (!errorMessage->isEmpty())
        return 0;

    runner->debug(debuggerHelper.absoluteFilePath());
    if (!runner->waitForStarted()) {
        *errorMessage = tr("Cannot start the WinRT Runner Tool.");
        return 0;
    }

    QLocalServer server;
    server.listen(QLatin1String("QtCreatorWinRtDebugPIDPipe"));
    if (!server.waitForNewConnection(10000)) {
        *errorMessage = tr("Cannot establish connection to the WinRT debugging helper.");
        return 0;
    }

    while (server.hasPendingConnections()) {
        QLocalSocket *connection = server.nextPendingConnection();
        if (connection->waitForReadyRead(1000)) {
            const QByteArray &output = connection->readAll();
            QList<QByteArray> arg = output.split(':');
            if (arg.first() == "PID") {
                bool ok =false;
                params.attachPID = arg.last().toInt(&ok);
                if (!ok) {
                    *errorMessage = tr("Cannot extract the PID from the WinRT debugging helper. "
                                       "(output: %1)").arg(QString::fromLocal8Bit(output));
                    return 0;
                }
                server.close();
                Debugger::DebuggerRunControl *debugRunControl
                        = DebuggerPlugin::createDebugger(params, runConfig, errorMessage);
                runner->setRunControl(debugRunControl);
                new WinRtDebugSupport(debugRunControl, runner);
                return debugRunControl;
            }
        }
    }

    server.close();

    *errorMessage = tr("Cannot create an appropriate run control for "
                       "the current run configuration.");

    return 0;
}

} // namespace Internal
} // namespace WinRt
