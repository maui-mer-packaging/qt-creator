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

#include "vcsplugin.h"

#include "vcsbaseconstants.h"

#include "commonsettingspage.h"
#include "nicknamedialog.h"
#include "vcsoutputwindow.h"
#include "corelistener.h"

#include <coreplugin/iversioncontrol.h>
#include <coreplugin/mimedatabase.h>
#include <coreplugin/variablemanager.h>
#include <coreplugin/vcsmanager.h>
#include <projectexplorer/project.h>
#include <projectexplorer/projectexplorer.h>

#include <QtPlugin>
#include <QDebug>

using namespace Core;
using namespace ProjectExplorer;

namespace VcsBase {
namespace Internal {

VcsPlugin *VcsPlugin::m_instance = 0;

VcsPlugin::VcsPlugin() :
    m_settingsPage(0),
    m_nickNameModel(0),
    m_coreListener(0)
{
    m_instance = this;
}

VcsPlugin::~VcsPlugin()
{
    m_instance = 0;
}

bool VcsPlugin::initialize(const QStringList &arguments, QString *errorMessage)
{
    Q_UNUSED(arguments)

    if (!Core::MimeDatabase::addMimeTypes(QLatin1String(":/vcsbase/VcsBase.mimetypes.xml"), errorMessage))
        return false;

    m_coreListener = new CoreListener;
    addAutoReleasedObject(m_coreListener);

    m_settingsPage = new CommonOptionsPage;
    addAutoReleasedObject(m_settingsPage);
    addAutoReleasedObject(VcsOutputWindow::instance());
    connect(m_settingsPage, SIGNAL(settingsChanged(VcsBase::Internal::CommonVcsSettings)),
            this, SIGNAL(settingsChanged(VcsBase::Internal::CommonVcsSettings)));
    connect(m_settingsPage, SIGNAL(settingsChanged(VcsBase::Internal::CommonVcsSettings)),
            this, SLOT(slotSettingsChanged()));
    slotSettingsChanged();

    VariableManager::registerVariable(Constants::VAR_VCS_NAME,
        tr("Name of the version control system in use by the current project."),
        []() -> QString {
            IVersionControl *vc = 0;
            if (Project *project = ProjectExplorerPlugin::currentProject())
                vc = VcsManager::findVersionControlForDirectory(project->projectDirectory().toString());
            return vc ? vc->displayName() : QString();
        });

    VariableManager::registerVariable(Constants::VAR_VCS_TOPIC,
        tr("The current version control topic (branch or tag) identification of the current project."),
        []() -> QString {
            IVersionControl *vc = 0;
            QString topLevel;
            if (Project *project = ProjectExplorerPlugin::currentProject())
                vc = VcsManager::findVersionControlForDirectory(project->projectDirectory().toString(), &topLevel);
            return vc ? vc->vcsTopic(topLevel) : QString();
        });

    VariableManager::registerVariable(Constants::VAR_VCS_TOPLEVELPATH,
        tr("The top level path to the repository the current project is in."),
        []() -> QString {
            if (Project *project = ProjectExplorerPlugin::currentProject())
                return VcsManager::findTopLevelForDirectory(project->projectDirectory().toString());
            return QString();
        });

    return true;
}

void VcsPlugin::extensionsInitialized()
{
}

VcsPlugin *VcsPlugin::instance()
{
    return m_instance;
}

CoreListener *VcsPlugin::coreListener() const
{
    return m_coreListener;
}

CommonVcsSettings VcsPlugin::settings() const
{
    return m_settingsPage->settings();
}

/* Delayed creation/update of the nick name model. */
QStandardItemModel *VcsPlugin::nickNameModel()
{
    if (!m_nickNameModel) {
        m_nickNameModel = NickNameDialog::createModel(this);
        populateNickNameModel();
    }
    return m_nickNameModel;
}

void VcsPlugin::populateNickNameModel()
{
    QString errorMessage;
    if (!NickNameDialog::populateModelFromMailCapFile(settings().nickNameMailMap,
                                                      m_nickNameModel,
                                                      &errorMessage)) {
        qWarning("%s", qPrintable(errorMessage));
    }
}

void VcsPlugin::slotSettingsChanged()
{
    if (m_nickNameModel)
        populateNickNameModel();
}

} // namespace Internal
} // namespace VcsBase
