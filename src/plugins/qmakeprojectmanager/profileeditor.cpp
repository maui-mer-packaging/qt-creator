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

#include "profileeditor.h"

#include "profilecompletionassist.h"
#include "profilehighlighter.h"
#include "qmakeprojectmanager.h"
#include "qmakeprojectmanagerconstants.h"
#include "qmakeprojectmanagerconstants.h"

#include <coreplugin/fileiconprovider.h>
#include <extensionsystem/pluginmanager.h>
#include <qtsupport/qtsupportconstants.h>
#include <texteditor/texteditoractionhandler.h>
#include <utils/qtcassert.h>

#include <QCoreApplication>
#include <QFileInfo>
#include <QDir>
#include <QTextBlock>

using namespace TextEditor;

namespace QmakeProjectManager {
namespace Internal {

class ProFileEditorWidget : public BaseTextEditorWidget
{
public:
    ProFileEditorWidget()
    {
        setCompletionAssistProvider(ExtensionSystem::PluginManager::getObject<ProFileCompletionAssistProvider>());
    }

protected:
    virtual Link findLinkAt(const QTextCursor &, bool resolveTarget = true,
                            bool inNextSplit = false);
    void contextMenuEvent(QContextMenuEvent *);
};

static bool isValidFileNameChar(const QChar &c)
{
    return c.isLetterOrNumber()
            || c == QLatin1Char('.')
            || c == QLatin1Char('_')
            || c == QLatin1Char('-')
            || c == QLatin1Char('/')
            || c == QLatin1Char('\\');
}

ProFileEditorWidget::Link ProFileEditorWidget::findLinkAt(const QTextCursor &cursor,
                                                          bool /*resolveTarget*/,
                                                          bool /*inNextSplit*/)
{
    Link link;

    int lineNumber = 0, positionInBlock = 0;
    convertPosition(cursor.position(), &lineNumber, &positionInBlock);

    const QString block = cursor.block().text();

    // check if the current position is commented out
    const int hashPos = block.indexOf(QLatin1Char('#'));
    if (hashPos >= 0 && hashPos < positionInBlock)
        return link;

    // find the beginning of a filename
    QString buffer;
    int beginPos = positionInBlock - 1;
    while (beginPos >= 0) {
        QChar c = block.at(beginPos);
        if (isValidFileNameChar(c)) {
            buffer.prepend(c);
            beginPos--;
        } else {
            break;
        }
    }

    // find the end of a filename
    int endPos = positionInBlock;
    while (endPos < block.count()) {
        QChar c = block.at(endPos);
        if (isValidFileNameChar(c)) {
            buffer.append(c);
            endPos++;
        } else {
            break;
        }
    }

    if (buffer.isEmpty())
        return link;

    // remove trailing '\' since it can be line continuation char
    if (buffer.at(buffer.size() - 1) == QLatin1Char('\\')) {
        buffer.chop(1);
        endPos--;
    }

    // if the buffer starts with $$PWD accept it
    if (buffer.startsWith(QLatin1String("PWD/")) ||
            buffer.startsWith(QLatin1String("PWD\\"))) {
        if (beginPos > 0 && block.mid(beginPos - 1, 2) == QLatin1String("$$")) {
            beginPos -=2;
            buffer = buffer.mid(4);
        }
    }

    QDir dir(QFileInfo(textDocument()->filePath()).absolutePath());
    QString fileName = dir.filePath(buffer);
    QFileInfo fi(fileName);
    if (fi.exists()) {
        if (fi.isDir()) {
            QDir subDir(fi.absoluteFilePath());
            QString subProject = subDir.filePath(subDir.dirName() + QLatin1String(".pro"));
            if (QFileInfo(subProject).exists())
                fileName = subProject;
            else
                return link;
        }
        link.targetFileName = QDir::cleanPath(fileName);
        link.linkTextStart = cursor.position() - positionInBlock + beginPos + 1;
        link.linkTextEnd = cursor.position() - positionInBlock + endPos;
    }
    return link;
}

void ProFileEditorWidget::contextMenuEvent(QContextMenuEvent *e)
{
    showDefaultContextMenu(e, Constants::M_CONTEXT);
}

//
// ProFileDocument
//

class ProFileDocument : public BaseTextDocument
{
public:
    ProFileDocument();
    QString defaultPath() const;
    QString suggestedFileName() const;

    // qmake project files doesn't support UTF8-BOM
    // If the BOM would be added qmake would fail and QtCreator couldn't parse the project file
    bool supportsUtf8Bom() { return false; }
};

ProFileDocument::ProFileDocument()
{
    setId(Constants::PROFILE_EDITOR_ID);
    setMimeType(QLatin1String(Constants::PROFILE_MIMETYPE));
    setSyntaxHighlighter(new ProFileHighlighter);
}

QString ProFileDocument::defaultPath() const
{
    QFileInfo fi(filePath());
    return fi.absolutePath();
}

QString ProFileDocument::suggestedFileName() const
{
    QFileInfo fi(filePath());
    return fi.fileName();
}

//
// ProFileEditorFactory
//

ProFileEditorFactory::ProFileEditorFactory()
{
    setId(Constants::PROFILE_EDITOR_ID);
    setDisplayName(qApp->translate("OpenWith::Editors", Constants::PROFILE_EDITOR_DISPLAY_NAME));
    addMimeType(Constants::PROFILE_MIMETYPE);
    addMimeType(Constants::PROINCLUDEFILE_MIMETYPE);
    addMimeType(Constants::PROFEATUREFILE_MIMETYPE);
    addMimeType(Constants::PROCONFIGURATIONFILE_MIMETYPE);
    addMimeType(Constants::PROCACHEFILE_MIMETYPE);
    addMimeType(Constants::PROSTASHFILE_MIMETYPE);

    setDocumentCreator([]() { return new ProFileDocument; });
    setEditorWidgetCreator([]() { return new ProFileEditorWidget; });

    setCommentStyle(Utils::CommentDefinition::HashStyle);
    setEditorActionHandlers(TextEditorActionHandler::UnCommentSelection
                | TextEditorActionHandler::JumpToFileUnderCursor);

    Core::FileIconProvider::registerIconOverlayForSuffix(QtSupport::Constants::ICON_QT_PROJECT, "pro");
    Core::FileIconProvider::registerIconOverlayForSuffix(QtSupport::Constants::ICON_QT_PROJECT, "pri");
    Core::FileIconProvider::registerIconOverlayForSuffix(QtSupport::Constants::ICON_QT_PROJECT, "prf");
}

} // namespace Internal
} // namespace QmakeProjectManager
