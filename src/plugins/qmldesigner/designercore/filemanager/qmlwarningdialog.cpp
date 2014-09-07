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

#include "qmlwarningdialog.h"
#include "ui_qmlwarningdialog.h"

#include <qmldesignerplugin.h>

#include <QPushButton>

namespace QmlDesigner {

namespace Internal {

QmlWarningDialog::QmlWarningDialog(QWidget *parent, const QStringList &warnings) :
    QDialog(parent),
    ui(new Ui::QmlWarningDialog),
    m_warnings(warnings)
{
    ui->setupUi(this);
    setResult (0);

    ui->checkBox->setChecked(true);
    connect(ui->buttonBox->button(QDialogButtonBox::Ignore),
            SIGNAL(clicked()), this, SLOT(ignoreButtonPressed()));
    connect(ui->buttonBox->button(QDialogButtonBox::Ok),
            SIGNAL(clicked()), this, SLOT(okButtonPressed()));
    connect(ui->checkBox, SIGNAL(toggled(bool)), this, SLOT(checkBoxToggled(bool)));

    connect(ui->warnings, SIGNAL(linkActivated(QString)), this, SLOT(linkClicked(QString)));

    QString warningText;
    foreach (const QString &string, warnings)
        warningText += QStringLiteral(" ") + string + QStringLiteral("\n");
    ui->warnings->setText(warningText);
}

QmlWarningDialog::~QmlWarningDialog()
{
    delete ui;
}

void QmlWarningDialog::ignoreButtonPressed()
{
    done(0);
}

void QmlWarningDialog::okButtonPressed()
{
    done(-1);
}

bool QmlWarningDialog::warningsEnabled() const
{
#ifndef QMLDESIGNER_TEST
    DesignerSettings settings = QmlDesignerPlugin::instance()->settings();
    return settings.warningsInDesigner;
#else
    return false;
#endif
}

void QmlWarningDialog::checkBoxToggled(bool b)
{
#ifndef QMLDESIGNER_TEST
    DesignerSettings settings = QmlDesignerPlugin::instance()->settings();
    settings.warningsInDesigner = b;
    QmlDesignerPlugin::instance()->setSettings(settings);
#else
    Q_UNUSED(b);
#endif
}

void QmlWarningDialog::linkClicked(const QString &link)
{
    done(link.toInt());
}

} //Internal
} //QmlDesigner
