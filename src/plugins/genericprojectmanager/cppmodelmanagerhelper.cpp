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

#include "cppmodelmanagerhelper.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>
#include <QThread>

using namespace CppTools;
using namespace GenericProjectManager::Internal::Tests;

CppModelManagerHelper::CppModelManagerHelper(QObject *parent) :
    QObject(parent)
{
    connect(cppModelManager(), SIGNAL(sourceFilesRefreshed(const QSet<QString> &)),
            this, SLOT(onSourceFilesRefreshed(const QSet<QString> &)));
}


CppModelManagerInterface *CppModelManagerHelper::cppModelManager()
{
    return CppModelManagerInterface::instance();
}

void CppModelManagerHelper::waitForSourceFilesRefreshed(const QString &file, int timeOut)
{
    waitForSourceFilesRefreshed(QStringList() << file, timeOut);
}

void CppModelManagerHelper::waitForSourceFilesRefreshed(const QStringList &files, int timeOut)
{
    QTime t;
    t.start();
    QSignalSpy spy(cppModelManager(), SIGNAL(sourceFilesRefreshed(const QSet<QString> &)));

    foreach (const QString &file, files) {
        while (!m_refreshedSourceFiles.contains(file)) {
#if QT_VERSION >= 0x050000
            QVERIFY(spy.wait());
#else // Qt 4.x
            class MyThread: public QThread
            {
            public:
                static void Zzzzz()
                {
                    QThread::msleep(50);
                }
            };

            MyThread::Zzzzz();
            QCoreApplication::processEvents();
#endif
        }
        QVERIFY(t.elapsed() <= timeOut);
    }
}

void CppModelManagerHelper::onSourceFilesRefreshed(const QSet<QString> &files)
{
    m_refreshedSourceFiles.unite(files);
}
