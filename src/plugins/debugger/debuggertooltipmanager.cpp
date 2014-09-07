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

#include "debuggertooltipmanager.h"
#include "debuggerinternalconstants.h"
#include "debuggerengine.h"
#include "debuggeractions.h"
#include "stackhandler.h"
#include "debuggercore.h"
#include "watchhandler.h"
#include "watchwindow.h"
#include "sourceutils.h"

#include <coreplugin/icore.h>
#include <coreplugin/coreconstants.h>
#include <coreplugin/editormanager/documentmodel.h>
#include <texteditor/basetexteditor.h>

#include <utils/tooltip/tooltip.h>
#include <utils/tooltip/tipcontents.h>
#include <utils/qtcassert.h>

#include <QApplication>
#include <QClipboard>
#include <QDebug>
#include <QDesktopWidget>
#include <QLabel>
#include <QScrollBar>
#include <QSortFilterProxyModel>
#include <QStack>
#include <QStandardItemModel>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

using namespace Core;
using namespace TextEditor;

// Expire tooltips after n days on (no longer load them) in order
// to avoid them piling up.
enum { toolTipsExpiryDays = 6 };

static const char sessionSettingsKeyC[] = "DebuggerToolTips";
static const char sessionDocumentC[] = "DebuggerToolTips";
static const char sessionVersionAttributeC[] = "version";
static const char toolTipElementC[] = "DebuggerToolTip";
static const char toolTipClassAttributeC[] = "class";
static const char fileNameAttributeC[] = "name";
static const char functionAttributeC[] = "function";
static const char textPositionAttributeC[] = "position";
static const char textLineAttributeC[] = "line";
static const char textColumnAttributeC[] = "column";
static const char offsetXAttributeC[] = "offset_x";
static const char offsetYAttributeC[] = "offset_y";
static const char engineTypeAttributeC[] = "engine";
static const char dateAttributeC[] = "date";
static const char treeElementC[] = "tree";
static const char treeExpressionAttributeC[] = "expression";
static const char treeInameAttributeC[] = "iname";
static const char modelElementC[] = "model";
static const char modelColumnCountAttributeC[] = "columncount";
static const char modelRowElementC[] = "row";
static const char modelItemElementC[] = "item";

// Forward a stream reader across end elements looking for the
// next start element of a desired type.
static bool readStartElement(QXmlStreamReader &r, const char *name)
{
    while (r.tokenType() != QXmlStreamReader::StartElement
            || r.name() != QLatin1String(name))
        switch (r.readNext()) {
        case QXmlStreamReader::EndDocument:
            return false;
        case QXmlStreamReader::NoToken:
        case QXmlStreamReader::Invalid:
            qWarning("'%s'/'%s' encountered while looking for start element '%s'.",
                    qPrintable(r.tokenString()),
                    qPrintable(r.name().toString()), name);
            return false;
        default:
            break;
        }
    return true;
}

#if 0
static void debugMode(const QAbstractItemModel *model)
{
    QDebug nospace = qDebug().nospace();
    nospace << model << '\n';
    for (int r = 0; r < model->rowCount(); r++)
        nospace << '#' << r << ' ' << model->data(model->index(r, 0)).toString() << '\n';
}
#endif

namespace Debugger {
namespace Internal {

// A label that can be dragged to drag something else.

class DraggableLabel : public QLabel
{
public:
    explicit DraggableLabel(QWidget *target)
        : m_target(target), m_moveStartPos(-1, -1), active(false)
    {}

    void mousePressEvent(QMouseEvent *event);
    void mouseReleaseEvent(QMouseEvent *event);
    void mouseMoveEvent(QMouseEvent *event);

public:
    QWidget *m_target;
    QPoint m_moveStartPos;
    QPoint m_offset;
    bool active;
};

void DraggableLabel::mousePressEvent(QMouseEvent * event)
{
    if (active && event->button() == Qt::LeftButton) {
        m_moveStartPos = event->globalPos();
        event->accept();
    }
    QLabel::mousePressEvent(event);
}

void DraggableLabel::mouseReleaseEvent(QMouseEvent * event)
{
    if (active && event->button() == Qt::LeftButton)
        m_moveStartPos = QPoint(-1, -1);
    QLabel::mouseReleaseEvent(event);
}

void DraggableLabel::mouseMoveEvent(QMouseEvent * event)
{
    if (active && (event->buttons() & Qt::LeftButton)) {
        if (m_moveStartPos != QPoint(-1, -1)) {
            const QPoint newPos = event->globalPos();
            const QPoint offset = newPos - m_moveStartPos;

            m_target->move(m_target->pos() + offset);
            m_offset += offset;

            m_moveStartPos = newPos;
        }
        event->accept();
    }
    QLabel::mouseMoveEvent(event);
}

// Helper for building a QStandardItemModel of a tree form (see TreeModelVisitor).
// The recursion/building is based on the scheme: \code
// <row><item1><item2>
//     <row><item11><item12></row>
// </row>
// \endcode

class StandardItemTreeModelBuilder
{
public:
    typedef QList<QStandardItem *> StandardItemRow;

    explicit StandardItemTreeModelBuilder(QStandardItemModel *m, Qt::ItemFlags f = Qt::ItemIsSelectable);

    void addItem(const QString &);
    void startRow();
    void endRow();

private:
    void pushRow();

    QStandardItemModel *m_model;
    const Qt::ItemFlags m_flags;
    StandardItemRow m_row;
    QStack<QStandardItem *> m_rowParents;
};

StandardItemTreeModelBuilder::StandardItemTreeModelBuilder(QStandardItemModel *m, Qt::ItemFlags f) :
    m_model(m), m_flags(f)
{
    m_model->removeRows(0, m_model->rowCount());
}

void StandardItemTreeModelBuilder::addItem(const QString &s)
{
    QStandardItem *item = new QStandardItem(s);
    item->setFlags(m_flags);
    m_row.push_back(item);
}

void StandardItemTreeModelBuilder::pushRow()
{
    if (m_rowParents.isEmpty())
        m_model->appendRow(m_row);
    else
        m_rowParents.top()->appendRow(m_row);
    m_rowParents.push(m_row.front());
    m_row.clear();
}

void StandardItemTreeModelBuilder::startRow()
{
    // Push parent in case rows are nested. This is a Noop for the very first row.
    if (!m_row.isEmpty())
        pushRow();
}

void StandardItemTreeModelBuilder::endRow()
{
    if (!m_row.isEmpty()) // Push row if no child rows have been encountered
        pushRow();
    m_rowParents.pop();
}

// Helper visitor base class for recursing over a tree model
// (see StandardItemTreeModelBuilder for the scheme).
class TreeModelVisitor
{
public:
    virtual void run() { run(QModelIndex()); }

protected:
    TreeModelVisitor(const QAbstractItemModel *model) : m_model(model) {}

    virtual void rowStarted() {}
    virtual void handleItem(const QModelIndex &m) = 0;
    virtual void rowEnded() {}

    const QAbstractItemModel *model() const { return m_model; }

private:
    void run(const QModelIndex &parent);

    const QAbstractItemModel *m_model;
};

void TreeModelVisitor::run(const QModelIndex &parent)
{
    const int columnCount = m_model->columnCount(parent);
    const int rowCount = m_model->rowCount(parent);
    for (int r = 0; r < rowCount; r++) {
        rowStarted();
        QModelIndex left;
        for (int c = 0; c < columnCount; c++) {
            const QModelIndex index = m_model->index(r, c, parent);
            handleItem(index);
            if (!c)
                left = index;
        }
        if (left.isValid())
            run(left);
        rowEnded();
    }
}

// Visitor writing out a tree model in XML format.
class XmlWriterTreeModelVisitor : public TreeModelVisitor
{
public:
    XmlWriterTreeModelVisitor(const QAbstractItemModel *model, QXmlStreamWriter &w);

    virtual void run();

protected:
    virtual void rowStarted() { m_writer.writeStartElement(QLatin1String(modelRowElementC)); }
    virtual void handleItem(const QModelIndex &m);
    virtual void rowEnded() { m_writer.writeEndElement(); }

private:
    const QString m_modelItemElement;
    QXmlStreamWriter &m_writer;
};

XmlWriterTreeModelVisitor::XmlWriterTreeModelVisitor(const QAbstractItemModel *model, QXmlStreamWriter &w) :
    TreeModelVisitor(model), m_modelItemElement(QLatin1String(modelItemElementC)), m_writer(w)
{
}

void XmlWriterTreeModelVisitor::run()
{
    m_writer.writeStartElement(QLatin1String(modelElementC));
    const int columnCount = model()->columnCount();
    m_writer.writeAttribute(QLatin1String(modelColumnCountAttributeC), QString::number(columnCount));
   TreeModelVisitor::run();
    m_writer.writeEndElement();
}

void XmlWriterTreeModelVisitor::handleItem(const QModelIndex &m)
{
    const QString value = m.data(Qt::DisplayRole).toString();
    if (value.isEmpty())
        m_writer.writeEmptyElement(m_modelItemElement);
    else
        m_writer.writeTextElement(m_modelItemElement, value);
}

// TreeModelVisitor for debugging/copying models
class DumpTreeModelVisitor : public TreeModelVisitor
{
public:
    enum Mode
    {
        DebugMode,      // For debugging, "|'data'|"
        ClipboardMode   // Tab-delimited "\tdata" for clipboard (see stack window)
    };
    explicit DumpTreeModelVisitor(const QAbstractItemModel *model, Mode m, QTextStream &s);

protected:
    virtual void rowStarted();
    virtual void handleItem(const QModelIndex &m);
    virtual void rowEnded();

private:
    const Mode m_mode;

    QTextStream &m_stream;
    int m_level;
    unsigned m_itemsInRow;
};

DumpTreeModelVisitor::DumpTreeModelVisitor(const QAbstractItemModel *model, Mode m, QTextStream &s) :
    TreeModelVisitor(model), m_mode(m), m_stream(s), m_level(0), m_itemsInRow(0)
{
    if (m_mode == DebugMode)
        m_stream << model->metaObject()->className() << '/' << model->objectName();
}

void DumpTreeModelVisitor::rowStarted()
{
    m_level++;
    if (m_itemsInRow) { // Nested row.
        m_stream << '\n';
        m_itemsInRow = 0;
    }
    switch (m_mode) {
    case DebugMode:
        m_stream << QString(2 * m_level, QLatin1Char(' '));
        break;
    case ClipboardMode:
        m_stream << QString(m_level, QLatin1Char('\t'));
        break;
    }
}

void DumpTreeModelVisitor::handleItem(const QModelIndex &m)
{
    const QString data = m.data().toString();
    switch (m_mode) {
    case DebugMode:
        if (m.column())
            m_stream << '|';
        m_stream << '\'' << data << '\'';
        break;
    case ClipboardMode:
        if (m.column())
            m_stream << '\t';
        m_stream << data;
        break;
    }
    m_itemsInRow++;
}

void DumpTreeModelVisitor::rowEnded()
{
    if (m_itemsInRow) {
        m_stream << '\n';
        m_itemsInRow = 0;
    }
    m_level--;
}

/*
static QDebug operator<<(QDebug d, const QAbstractItemModel &model)
{
    QString s;
    QTextStream str(&s);
    Debugger::Internal::DumpTreeModelVisitor v(&model, Debugger::Internal::DumpTreeModelVisitor::DebugMode, str);
    v.run();
    qDebug().nospace() << s;
    return d;
}
*/

/*!
    \class Debugger::Internal::TooltipFilterModel

    \brief The TooltipFilterModel class is a model for tooltips filtering an
    item on the watchhandler matching its tree on the iname.

    In addition, suppress the model's tooltip data to avoid a tooltip on a tooltip.
*/

class TooltipFilterModel : public QSortFilterProxyModel
{
public:
    TooltipFilterModel() {}

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const
    {
        return role == Qt::ToolTipRole
            ? QVariant() : QSortFilterProxyModel::data(index, role);
    }

    static bool isSubIname(const QByteArray &haystack, const QByteArray &needle)
    {
        return haystack.size() > needle.size()
           && haystack.startsWith(needle)
           && haystack.at(needle.size()) == '.';
    }

    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
    {
        const QModelIndex nameIndex = sourceModel()->index(sourceRow, 0, sourceParent);
        const QByteArray iname = nameIndex.data(LocalsINameRole).toByteArray();
//        qDebug() << "ACCEPTING FILTER" << iname
//                 << (iname == m_iname || isSubIname(iname, m_iname) || isSubIname(m_iname, iname));
        return iname == m_iname || isSubIname(iname, m_iname) || isSubIname(m_iname, iname);
    }

    QByteArray m_iname;
};

/////////////////////////////////////////////////////////////////////////
//
// TreeModelCopyVisitor builds a QStandardItem from a tree model (copy).
//
/////////////////////////////////////////////////////////////////////////

class TreeModelCopyVisitor : public TreeModelVisitor
{
public:
    TreeModelCopyVisitor(const QAbstractItemModel *source, QStandardItemModel *target) :
        TreeModelVisitor(source), m_builder(target)
    {}

protected:
    virtual void rowStarted() { m_builder.startRow(); }
    virtual void handleItem(const QModelIndex &m) { m_builder.addItem(m.data().toString()); }
    virtual void rowEnded() { m_builder.endRow(); }

private:
    StandardItemTreeModelBuilder m_builder;
};

class DebuggerToolTipWidget;
class DebuggerToolTipManagerData
{
public:
    DebuggerToolTipManagerData()
        : m_debugModeActive(false), m_lastToolTipPoint(-1, -1), m_lastToolTipEditor(0)
    {}

    void purgeClosedToolTips()
    {
        for (int i = m_tooltips.size(); --i >= 0; )
            if (!m_tooltips.at(i))
                m_tooltips.removeAt(i);
    }

    QList<QPointer<DebuggerToolTipWidget> > m_tooltips;
    bool m_debugModeActive;
    QPoint m_lastToolTipPoint;
    Core::IEditor *m_lastToolTipEditor;
};

static DebuggerToolTipManagerData *d = 0;


/////////////////////////////////////////////////////////////////////////
//
// DebuggerToolTipWidget
//
/////////////////////////////////////////////////////////////////////////

class DebuggerToolTipWidget : public QWidget
{
    Q_OBJECT

public:
    DebuggerToolTipWidget(const DebuggerToolTipContext &context);

    bool isPinned() const  { return m_isPinned; }
    QString fileName() const { return m_context.fileName; }
    QString function() const { return m_context.function; }
    int position() const { return m_context.position; }

    const DebuggerToolTipContext &context() const { return m_context; }

    void acquireEngine();
    void releaseEngine();

    void saveSessionData(QXmlStreamWriter &w) const;
    void setWatchModel(QAbstractItemModel *watchModel);
    void handleStackFrameCompleted(const QString &frameFile, const QString &frameFunction);

public slots:
    void handleItemIsExpanded(const QModelIndex &sourceIdx)
    {
        QTC_ASSERT(m_filterModel.sourceModel() == sourceIdx.model(), return);
        QModelIndex mappedIdx = m_filterModel.mapFromSource(sourceIdx);
        if (!m_treeView->isExpanded(mappedIdx))
            m_treeView->expand(mappedIdx);
    }

    void copy();
    void positionShow(const BaseTextEditorWidget *editorWidget);
    void pin();

public:
    bool m_isPinned;
    QToolButton *m_toolButton;
    DraggableLabel *m_titleLabel;
    QDate m_creationDate;
    DebuggerToolTipTreeView *m_treeView; //!< Pointing to either m_defaultModel oder m_filterModel
    DebuggerToolTipContext m_context;
    TooltipFilterModel m_filterModel; //!< Pointing to a valid watchModel
    QStandardItemModel m_defaultModel;
};

void DebuggerToolTipWidget::pin()
{
    if (m_isPinned)
        return;
    m_isPinned = true;
    m_toolButton->setIcon(style()->standardIcon(QStyle::SP_DockWidgetCloseButton));

    if (parentWidget()) {
        // We are currently within a text editor tooltip:
        // Rip out of parent widget and re-show as a tooltip
        Utils::WidgetContent::pinToolTip(this);
    } else {
        // We have just be restored from session data.
        setWindowFlags(Qt::ToolTip);
    }
    m_titleLabel->active = true; // User can now drag
}

/*!
    \class Debugger::Internal::DebuggerToolTipContext

    \brief The DebuggerToolTipContext class specifies the file name and
    position where the tooltip is anchored.

    Uses redundant position or line column information to detect whether
    the underlying file has been changed
    on restoring.
*/

DebuggerToolTipContext::DebuggerToolTipContext()
    : position(0), line(0), column(0)
{
}

bool DebuggerToolTipContext::matchesFrame(const QString &frameFile, const QString &frameFunction) const
{
    return (fileName.isEmpty() || frameFile.isEmpty() || fileName == frameFile)
            && (function.isEmpty() || frameFunction.isEmpty() || function == frameFunction);
}

bool DebuggerToolTipContext::isSame(const DebuggerToolTipContext &other) const
{
    return fileName == other.fileName
            && function == other.function
            && iname == other.iname;
}

QDebug operator<<(QDebug d, const DebuggerToolTipContext &c)
{
    QDebug nsp = d.nospace();
    nsp << c.fileName << '@' << c.line << ',' << c.column << " (" << c.position << ')' << "INAME: " << c.iname << " EXP: " << c.expression;
    if (!c.function.isEmpty())
        nsp << ' ' << c.function << "()";
    return d;
}

/*!
    \class Debugger::Internal::DebuggerToolTipWidget

    \brief The DebuggerToolTipWidget class is a pinnable debugger tool tip
    widget.

    The debugger tooltip goes from the unpinned state (button
    showing 'Pin') to the pinned state (button showing 'Close').
    It consists of a title toolbar and a vertical main layout.
    The widget has the ability to save/restore tree model contents to XML.
    With the engine acquired, it sets a filter model (by expression) on
    one of the engine's models (debuggerModel).
    On release, it serializes and restores the data to a QStandardItemModel
    (defaultModel) and displays that.

    It is associated with file name and position with functionality to
    acquire and release the engine. When the debugger stops at a file, all
    matching tooltips acquire the engine, that is, display the engine data.
    When continuing or switching away from the frame, the tooltips release the
    engine, that is, store the data internally and keep displaying them
    marked as 'previous'.

    When restoring the data from a session, all tooltips start in 'released' mode.

    Stored tooltips expire after toolTipsExpiryDays while loading to prevent
    them accumulating.

    In addition, if the stored line number diverges too much from the current line
    number in positionShow(), the tooltip is also closed/discarded.

    The widget is that is first shown by the TextEditor's tooltip
    class and typically closed by it unless the user pins it.
    In that case, it is removed from the tip's layout, added to the DebuggerToolTipManager's
    list of pinned tooltips and re-shown as a global tooltip widget.
    As the debugger stop and continues, it shows the debugger values or a copy
    of them. On closing or session changes, the contents it saved.
*/


static QString msgReleasedText() { return DebuggerToolTipWidget::tr("Previous"); }

DebuggerToolTipWidget::DebuggerToolTipWidget(const DebuggerToolTipContext &context)
{
    setFocusPolicy(Qt::NoFocus);

    m_isPinned = false;
    m_context = context;
    m_filterModel.m_iname = context.iname;

    const QIcon pinIcon(QLatin1String(":/debugger/images/pin.xpm"));

    m_toolButton = new QToolButton;
    m_toolButton->setIcon(pinIcon);

    auto copyButton = new QToolButton;
    copyButton->setIcon(QIcon(QLatin1String(Core::Constants::ICON_COPY)));

    m_titleLabel = new DraggableLabel(this);
    m_titleLabel->setText(msgReleasedText());
    m_titleLabel->setMinimumWidth(40); // Ensure a draggable area even if text is empty.

    auto toolBar = new QToolBar(this);
    toolBar->setProperty("_q_custom_style_disabled", QVariant(true));
    const QList<QSize> pinIconSizes = pinIcon.availableSizes();
    if (!pinIconSizes.isEmpty())
        toolBar->setIconSize(pinIconSizes.front());
    toolBar->addWidget(m_toolButton);
    toolBar->addWidget(m_titleLabel);
    toolBar->addWidget(copyButton);

    m_treeView = new DebuggerToolTipTreeView(this);
    m_treeView->setFocusPolicy(Qt::NoFocus);

    auto mainLayout = new QVBoxLayout(this);
    mainLayout->setSizeConstraint(QLayout::SetFixedSize);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->addWidget(toolBar);
    mainLayout->addWidget(m_treeView);

    connect(m_toolButton, &QAbstractButton::clicked, [this]() {
        if (m_isPinned)
            close();
        else
            pin();
    });

    connect(copyButton, &QAbstractButton::clicked, this, &DebuggerToolTipWidget::copy);
}

void DebuggerToolTipWidget::setWatchModel(QAbstractItemModel *watchModel)
{
    QTC_ASSERT(watchModel, return);
    m_filterModel.setSourceModel(watchModel);
    connect(watchModel, SIGNAL(itemIsExpanded(QModelIndex)),
            this, SLOT(handleItemIsExpanded(QModelIndex)), Qt::UniqueConnection);
    connect(watchModel, SIGNAL(columnAdjustmentRequested()),
            m_treeView, SLOT(computeSize()), Qt::UniqueConnection);
}

void DebuggerToolTipWidget::handleStackFrameCompleted(const QString &frameFile, const QString &frameFunction)
{
    const bool sameFrame = m_context.matchesFrame(frameFile, frameFunction);
    const bool isAcquired = m_treeView->model() == &m_filterModel;
    if (isAcquired && !sameFrame)
       releaseEngine();
    else if (!isAcquired && sameFrame)
       acquireEngine();

    if (isAcquired) {
        m_treeView->expand(m_filterModel.index(0, 0));
        WatchTreeView::reexpand(m_treeView, m_filterModel.index(0, 0));
    }
}

void DebuggerToolTipWidget::acquireEngine()
{
    m_titleLabel->setText(m_context.expression);
    m_treeView->setModel(&m_filterModel);
    m_treeView->setRootIndex(m_filterModel.index(0, 0));
    m_treeView->expand(m_filterModel.index(0, 0));
    WatchTreeView::reexpand(m_treeView, m_filterModel.index(0, 0));
}

void DebuggerToolTipWidget::releaseEngine()
{
    // Save data to stream and restore to the backup m_defaultModel.
    m_defaultModel.removeRows(0, m_defaultModel.rowCount());
    TreeModelCopyVisitor v(&m_filterModel, &m_defaultModel);
    v.run();

    m_titleLabel->setText(msgReleasedText());
    m_treeView->setModel(&m_defaultModel);
    m_treeView->setRootIndex(m_defaultModel.index(0, 0));
    m_treeView->expandAll();
}

void DebuggerToolTipWidget::copy()
{
    QString clipboardText = DebuggerToolTipManager::treeModelClipboardContents(m_treeView->model());
    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setText(clipboardText, QClipboard::Selection);
    clipboard->setText(clipboardText, QClipboard::Clipboard);
}

void DebuggerToolTipWidget::positionShow(const BaseTextEditorWidget *editorWidget)
{
    // Figure out new position of tooltip using the text edit.
    // If the line changed too much, close this tip.
    QTC_ASSERT(editorWidget, return);
    QTextCursor cursor = editorWidget->textCursor();
    cursor.setPosition(m_context.position);
    const int line = cursor.blockNumber();
    if (qAbs(m_context.line - line) > 2) {
        close();
        return ;
    }

    const QPoint screenPos = editorWidget->toolTipPosition(cursor) + m_titleLabel->m_offset;
    const QRect toolTipArea = QRect(screenPos, QSize(sizeHint()));
    const QRect plainTextArea = QRect(editorWidget->mapToGlobal(QPoint(0, 0)), editorWidget->size());
    const bool visible = plainTextArea.intersects(toolTipArea);
    //    qDebug() << "DebuggerToolTipWidget::positionShow() " << this << m_context
    //             << " line: " << line << " plainTextPos " << toolTipArea
    //             << " offset: " << m_titleLabel->m_offset
    //             << " Area: " << plainTextArea << " Screen pos: "
    //             << screenPos << te.widget << " visible=" << visible;

    if (!visible) {
        hide();
        return;
    }

    move(screenPos);
    show();
}

static DebuggerToolTipWidget *findOrCreateWidget(const DebuggerToolTipContext &context)
{
    foreach (const QPointer<DebuggerToolTipWidget> &tw, d->m_tooltips)
        if (tw && tw->m_context.isSame(context))
            return tw;

    auto tw = new DebuggerToolTipWidget(context);
    tw->setAttribute(Qt::WA_DeleteOnClose);
    tw->setObjectName(QLatin1String("DebuggerTreeViewToolTipWidget: ") + QLatin1String(context.iname));
    tw->m_context.creationDate = QDate::currentDate();

    d->m_tooltips.push_back(tw);

    return tw;
}

static void restoreTreeModel(QXmlStreamReader &r, QStandardItemModel *m)
{
    StandardItemTreeModelBuilder builder(m);
    int columnCount = 1;
    bool withinModel = true;
    while (withinModel && !r.atEnd()) {
        const QXmlStreamReader::TokenType token = r.readNext();
        switch (token) {
        case QXmlStreamReader::StartElement: {
            const QStringRef element = r.name();
            // Root model element with column count.
            if (element == QLatin1String(modelElementC)) {
                if (const int cc = r.attributes().value(QLatin1String(modelColumnCountAttributeC)).toString().toInt())
                    columnCount = cc;
                m->setColumnCount(columnCount);
            } else if (element == QLatin1String(modelRowElementC)) {
                builder.startRow();
            } else if (element == QLatin1String(modelItemElementC)) {
                builder.addItem(r.readElementText());
            }
        }
            break; // StartElement
        case QXmlStreamReader::EndElement: {
            const QStringRef element = r.name();
            // Row closing: pop off parent.
            if (element == QLatin1String(modelRowElementC))
                builder.endRow();
            else if (element == QLatin1String(modelElementC))
                withinModel = false;
        }
            break; // EndElement
        default:
            break;
        } // switch
    } // while
}

// Parse a 'yyyyMMdd' date
static QDate dateFromString(const QString &date)
{
    return date.size() == 8 ?
        QDate(date.left(4).toInt(), date.mid(4, 2).toInt(), date.mid(6, 2).toInt()) :
        QDate();
}

static void loadSessionDataI(QXmlStreamReader &r)
{
    if (!readStartElement(r, toolTipElementC))
        return;
    const QXmlStreamAttributes attributes = r.attributes();
    DebuggerToolTipContext context;
    context.fileName = attributes.value(QLatin1String(fileNameAttributeC)).toString();
    context.position = attributes.value(QLatin1String(textPositionAttributeC)).toString().toInt();
    context.line = attributes.value(QLatin1String(textLineAttributeC)).toString().toInt();
    context.column = attributes.value(QLatin1String(textColumnAttributeC)).toString().toInt();
    context.function = attributes.value(QLatin1String(functionAttributeC)).toString();
    QPoint offset;
    const QString offsetXAttribute = QLatin1String(offsetXAttributeC);
    const QString offsetYAttribute = QLatin1String(offsetYAttributeC);
    if (attributes.hasAttribute(offsetXAttribute))
        offset.setX(attributes.value(offsetXAttribute).toString().toInt());
    if (attributes.hasAttribute(offsetYAttribute))
        offset.setY(attributes.value(offsetYAttribute).toString().toInt());
    context.mousePosition = offset;

    context.iname = attributes.value(QLatin1String(treeInameAttributeC)).toString().toLatin1();
    context.expression = attributes.value(QLatin1String(treeExpressionAttributeC)).toString();

    const QStringRef className = attributes.value(QLatin1String(toolTipClassAttributeC));
    context.engineType = attributes.value(QLatin1String(engineTypeAttributeC)).toString();
    context.creationDate = dateFromString(attributes.value(QLatin1String(dateAttributeC)).toString());
    bool readTree = context.isValid();
    if (!context.creationDate.isValid() || context.creationDate.daysTo(QDate::currentDate()) > toolTipsExpiryDays) {
        // qDebug() << "Expiring tooltip " << context.fileName << '@' << context.position << " from " << creationDate;
        //readTree = false;
    } else if (className != QLatin1String("Debugger::Internal::DebuggerToolTipWidget")) {
        qWarning("Unable to create debugger tool tip widget of class %s", qPrintable(className.toString()));
        readTree = false;
    }

    if (readTree) {
        DebuggerToolTipWidget *tw = findOrCreateWidget(context);
        restoreTreeModel(r, &tw->m_defaultModel);
        tw->pin();
        tw->acquireEngine();
        tw->m_titleLabel->setText(DebuggerToolTipManager::tr("Restored"));
        tw->m_treeView->setModel(&tw->m_defaultModel);
        tw->m_treeView->setRootIndex(tw->m_defaultModel.index(0, 0));
        tw->m_treeView->expandAll();
    } else {
        r.readElementText(QXmlStreamReader::SkipChildElements); // Skip
    }

    r.readNext(); // Skip </tree>
}

void DebuggerToolTipWidget::saveSessionData(QXmlStreamWriter &w) const
{
    w.writeStartElement(QLatin1String(toolTipElementC));
    QXmlStreamAttributes attributes;
    attributes.append(QLatin1String(toolTipClassAttributeC), QString::fromLatin1(metaObject()->className()));
    attributes.append(QLatin1String(fileNameAttributeC), m_context.fileName);
    if (!m_context.function.isEmpty())
        attributes.append(QLatin1String(functionAttributeC), m_context.function);
    attributes.append(QLatin1String(textPositionAttributeC), QString::number(m_context.position));
    attributes.append(QLatin1String(textLineAttributeC), QString::number(m_context.line));
    attributes.append(QLatin1String(textColumnAttributeC), QString::number(m_context.column));
    attributes.append(QLatin1String(dateAttributeC), m_creationDate.toString(QLatin1String("yyyyMMdd")));
    if (m_titleLabel->m_offset.x())
        attributes.append(QLatin1String(offsetXAttributeC), QString::number(m_titleLabel->m_offset.x()));
    if (m_titleLabel->m_offset.y())
        attributes.append(QLatin1String(offsetYAttributeC), QString::number(m_titleLabel->m_offset.y()));
    attributes.append(QLatin1String(engineTypeAttributeC), m_context.engineType);
    attributes.append(QLatin1String(treeExpressionAttributeC), m_context.expression);
    attributes.append(QLatin1String(treeInameAttributeC), QLatin1String(m_context.iname));
    w.writeAttributes(attributes);

        w.writeStartElement(QLatin1String(treeElementC));
        XmlWriterTreeModelVisitor v(&m_filterModel, w);
        v.run();
        w.writeEndElement();

    w.writeEndElement();
}

/*!
    \class Debugger::Internal::DebuggerToolTipTreeView

    \brief The DebuggerToolTipTreeView class is a treeview that adapts its size
    to the model contents (also while expanding)
    to be used within DebuggerTreeViewToolTipWidget.

*/

DebuggerToolTipTreeView::DebuggerToolTipTreeView(QWidget *parent) :
    QTreeView(parent)
{
    setHeaderHidden(true);
    setEditTriggers(NoEditTriggers);

    setUniformRowHeights(true);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    connect(this, SIGNAL(collapsed(QModelIndex)), this, SLOT(computeSize()),
        Qt::QueuedConnection);
    connect(this, SIGNAL(expanded(QModelIndex)), this, SLOT(computeSize()),
        Qt::QueuedConnection);

    connect(this, SIGNAL(expanded(QModelIndex)),
        SLOT(expandNode(QModelIndex)));
    connect(this, SIGNAL(collapsed(QModelIndex)),
        SLOT(collapseNode(QModelIndex)));
}

void DebuggerToolTipTreeView::expandNode(const QModelIndex &idx)
{
    model()->setData(idx, true, LocalsExpandedRole);
}

void DebuggerToolTipTreeView::collapseNode(const QModelIndex &idx)
{
    model()->setData(idx, false, LocalsExpandedRole);
}

int DebuggerToolTipTreeView::computeHeight(const QModelIndex &index) const
{
    int s = rowHeight(index);
    const int rowCount = model()->rowCount(index);
    for (int i = 0; i < rowCount; ++i)
        s += computeHeight(model()->index(i, 0, index));
    return s;
}

void DebuggerToolTipTreeView::computeSize()
{
    int columns = 30; // Decoration
    int rows = 0;
    bool rootDecorated = false;

    if (QAbstractItemModel *m = model()) {
        WatchTreeView::reexpand(this, m->index(0, 0));
        const int columnCount = m->columnCount();
        rootDecorated = m->rowCount() > 0;
        if (rootDecorated)
        for (int i = 0; i < columnCount; ++i) {
            resizeColumnToContents(i);
            columns += sizeHintForColumn(i);
        }
        if (columns < 100)
            columns = 100; // Prevent toolbar from shrinking when displaying 'Previous'
        rows += computeHeight(QModelIndex());

        // Fit tooltip to screen, showing/hiding scrollbars as needed.
        // Add a bit of space to account for tooltip border, and not
        // touch the border of the screen.
        QPoint pos(x(), y());
        QTC_ASSERT(QApplication::desktop(), return);
        QRect desktopRect = QApplication::desktop()->availableGeometry(pos);
        const int maxWidth = desktopRect.right() - pos.x() - 5 - 5;
        const int maxHeight = desktopRect.bottom() - pos.y() - 5 - 5;

        if (columns > maxWidth)
            rows += horizontalScrollBar()->height();

        if (rows > maxHeight) {
            setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
            rows = maxHeight;
            columns += verticalScrollBar()->width();
        } else {
            setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        }

        if (columns > maxWidth) {
            setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
            columns = maxWidth;
        } else {
            setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        }
    }

    m_size = QSize(columns + 5, rows + 5);
    setMinimumSize(m_size);
    setMaximumSize(m_size);
    setRootIsDecorated(rootDecorated);
}

QString DebuggerToolTipManager::treeModelClipboardContents(const QAbstractItemModel *model)
{
    QString rc;
    QTC_ASSERT(model, return rc);
    QTextStream str(&rc);
    DumpTreeModelVisitor v(model, DumpTreeModelVisitor::ClipboardMode, str);
    v.run();
    return rc;
}

/*!
    \class Debugger::Internal::DebuggerToolTipManager

    \brief The DebuggerToolTipManager class manages the pinned tooltip widgets,
    listens on editor scroll and main window move
    events and takes care of repositioning the tooltips.

    Listens to editor change and mode change. In debug mode, if there are tooltips
    for the current editor (by file name), positions and shows them.

    In addition, listens on state change and stack frame completed signals
    of the engine. If a stack frame is completed, has all matching tooltips
    (by file name and function) acquire the engine, others release.
*/

static DebuggerToolTipManager *m_instance = 0;

DebuggerToolTipManager::DebuggerToolTipManager(QObject *parent) :
    QObject(parent)
{
    d = new DebuggerToolTipManagerData;
    m_instance = this;
}

DebuggerToolTipManager::~DebuggerToolTipManager()
{
    delete d;
    m_instance = 0;
}

void DebuggerToolTipManager::registerEngine(DebuggerEngine *)
{
    loadSessionData();
}

void DebuggerToolTipManager::updateEngine(DebuggerEngine *engine)
{
    QTC_ASSERT(engine, return);
    d->purgeClosedToolTips();
    if (d->m_tooltips.isEmpty())
        return;

    // Stack frame changed: All tooltips of that file acquire the engine,
    // all others release (arguable, this could be more precise?)
    QString fileName;
    QString function;
    const int index = engine->stackHandler()->currentIndex();
    if (index >= 0) {
        const StackFrame frame = engine->stackHandler()->currentFrame();
        if (frame.usable) {
            fileName = frame.file;
            function = frame.function;
        }
    }
    foreach (const QPointer<DebuggerToolTipWidget> &tw, d->m_tooltips)
        tw->handleStackFrameCompleted(fileName, function);
    slotUpdateVisibleToolTips(); // Move out when stepping in same file.
}

void DebuggerToolTipManager::deregisterEngine(DebuggerEngine *engine)
{
    QTC_ASSERT(engine, return);
    foreach (const QPointer<DebuggerToolTipWidget> &tw, d->m_tooltips)
        if (tw && tw->m_context.engineType == engine->objectName())
            tw->releaseEngine();
    saveSessionData();
}

bool DebuggerToolTipManager::hasToolTips()
{
    return !d->m_tooltips.isEmpty();
}

void DebuggerToolTipManager::showToolTip
    (const DebuggerToolTipContext &context, DebuggerEngine *engine)
{
    QTC_ASSERT(engine, return);
    QTC_ASSERT(!context.expression.isEmpty(), qDebug(" BUT EMPTY"); return);

    DebuggerToolTipWidget *tw = findOrCreateWidget(context);
    tw->setWatchModel(engine->watchHandler()->model());
    tw->acquireEngine();

    const Utils::WidgetContent widgetContent(tw, true);
    Utils::ToolTip::show(context.mousePosition, widgetContent, debuggerCore()->mainWindow());
}

bool DebuggerToolTipManager::eventFilter(QObject *o, QEvent *e)
{
    if (!hasToolTips())
        return false;
    switch (e->type()) {
    case QEvent::Move: { // Move along with parent (toplevel)
        const QMoveEvent *me = static_cast<const QMoveEvent *>(e);
        const QPoint dist = me->pos() - me->oldPos();
        d->purgeClosedToolTips();
        foreach (const QPointer<DebuggerToolTipWidget> &tw, d->m_tooltips)
            if (tw->isVisible())
                tw->move(tw->pos() + dist);
        }
        break;
    case QEvent::WindowStateChange: { // Hide/Show along with parent (toplevel)
        const QWindowStateChangeEvent *se = static_cast<const QWindowStateChangeEvent *>(e);
        const bool wasMinimized = se->oldState() & Qt::WindowMinimized;
        const bool isMinimized  = static_cast<const QWidget *>(o)->windowState() & Qt::WindowMinimized;
        if (wasMinimized ^ isMinimized) {
            d->purgeClosedToolTips();
            foreach (const QPointer<DebuggerToolTipWidget> &tw, d->m_tooltips)
                tw->setVisible(!isMinimized);
        }
    }
        break;
    default:
        break;
    }
    return false;
}

void DebuggerToolTipManager::sessionAboutToChange()
{
    closeAllToolTips();
}

void DebuggerToolTipManager::loadSessionData()
{
    const QString data = sessionValue(sessionSettingsKeyC).toString();
    QXmlStreamReader r(data);
    r.readNextStartElement();
    if (r.tokenType() == QXmlStreamReader::StartElement && r.name() == QLatin1String(sessionDocumentC))
        while (!r.atEnd())
            loadSessionDataI(r);
}

void DebuggerToolTipManager::saveSessionData()
{
    QString data;
    d->purgeClosedToolTips();

    QXmlStreamWriter w(&data);
    w.writeStartDocument();
    w.writeStartElement(QLatin1String(sessionDocumentC));
    w.writeAttribute(QLatin1String(sessionVersionAttributeC), QLatin1String("1.0"));
    foreach (const QPointer<DebuggerToolTipWidget> &tw, d->m_tooltips)
        if (tw->isPinned())
            tw->saveSessionData(w);
    w.writeEndDocument();

    setSessionValue(sessionSettingsKeyC, QVariant(data));
}

void DebuggerToolTipManager::closeAllToolTips()
{
    d->purgeClosedToolTips();
    foreach (const QPointer<DebuggerToolTipWidget> &tw, d->m_tooltips)
        tw->close();
    d->m_tooltips.clear();
}

void DebuggerToolTipManager::hide()
{
    d->purgeClosedToolTips();
    foreach (const QPointer<DebuggerToolTipWidget> &tw, d->m_tooltips)
        tw->hide();
}

void DebuggerToolTipManager::slotUpdateVisibleToolTips()
{
    d->purgeClosedToolTips();
    if (d->m_tooltips.isEmpty())
        return;
    if (!d->m_debugModeActive) {
        hide();
        return;
    }

    BaseTextEditor *toolTipEditor = BaseTextEditor::currentTextEditor();
    if (!toolTipEditor) {
        hide();
        return;
    }

    const QString fileName = toolTipEditor->textDocument()->filePath();
    if (fileName.isEmpty()) {
        hide();
        return;
    }

    // Reposition and show all tooltips of that file.
    foreach (const QPointer<DebuggerToolTipWidget> &tw, d->m_tooltips) {
        if (tw->fileName() == fileName)
            tw->positionShow(toolTipEditor->editorWidget());
        else
            tw->hide();
    }
}

void DebuggerToolTipManager::slotDebuggerStateChanged(DebuggerState state)
{
    const QObject *engine = sender();
    QTC_ASSERT(engine, return);

    // Release at earliest possible convenience.
    switch (state) {
    case InferiorShutdownRequested:
    case EngineShutdownRequested:
    case DebuggerFinished:
    case EngineShutdownOk: {
        break;
    }
    default:
        break;
    }
}

void DebuggerToolTipManager::slotEditorOpened(IEditor *e)
{
    // Move tooltip along when scrolled.
    if (BaseTextEditor *textEditor = qobject_cast<BaseTextEditor *>(e)) {
        BaseTextEditorWidget *widget = textEditor->editorWidget();
        connect(widget->verticalScrollBar(), &QScrollBar::valueChanged,
                this, &DebuggerToolTipManager::slotUpdateVisibleToolTips);
        connect(textEditor, &BaseTextEditor::tooltipOverrideRequested,
                this, &DebuggerToolTipManager::slotTooltipOverrideRequested);
    }
}

void DebuggerToolTipManager::debugModeEntered()
{
    // Hook up all signals in debug mode.
    if (!d->m_debugModeActive) {
        d->m_debugModeActive = true;
        QWidget *topLevel = ICore::mainWindow()->topLevelWidget();
        topLevel->installEventFilter(this);
        EditorManager *em = EditorManager::instance();
        connect(em, &EditorManager::currentEditorChanged,
                this, &DebuggerToolTipManager::slotUpdateVisibleToolTips);
        connect(em, &EditorManager::editorOpened,
                this, &DebuggerToolTipManager::slotEditorOpened);
        foreach (IEditor *e, DocumentModel::editorsForOpenedDocuments())
            slotEditorOpened(e);
        // Position tooltips delayed once all the editor placeholder layouting is done.
        if (!d->m_tooltips.isEmpty())
            QTimer::singleShot(0, this, SLOT(slotUpdateVisibleToolTips()));
    }
}

void DebuggerToolTipManager::leavingDebugMode()
{
    // Remove all signals in debug mode.
    if (d->m_debugModeActive) {
        d->m_debugModeActive = false;
        hide();
        if (QWidget *topLevel = ICore::mainWindow()->topLevelWidget())
            topLevel->removeEventFilter(this);
        foreach (IEditor *e, DocumentModel::editorsForOpenedDocuments()) {
            if (BaseTextEditor *toolTipEditor = qobject_cast<BaseTextEditor *>(e)) {
                toolTipEditor->editorWidget()->verticalScrollBar()->disconnect(this);
                toolTipEditor->disconnect(this);
            }
        }
        EditorManager::instance()->disconnect(this);
        d->m_lastToolTipEditor = 0;
        d->m_lastToolTipPoint = QPoint(-1, -1);
    }
}

void DebuggerToolTipManager::slotTooltipOverrideRequested
    (BaseTextEditor *editor, const QPoint &point, int pos, bool *handled)
{
    QTC_ASSERT(handled, return);
    QTC_ASSERT(editor, return);

    const int movedDistance = (point - d->m_lastToolTipPoint).manhattanLength();
    if (d->m_lastToolTipEditor == editor && movedDistance < 25) {
        *handled = true;
        return;
    }

    *handled = tryHandleToolTipOverride(editor, point, pos);

    if (*handled) {
        d->m_lastToolTipEditor = editor;
        d->m_lastToolTipPoint = point;
    } else {
        d->m_lastToolTipEditor = 0;
        d->m_lastToolTipPoint = QPoint(-1, -1);
    }
}

bool DebuggerToolTipManager::tryHandleToolTipOverride(BaseTextEditor *editor, const QPoint &point, int pos)
{
    if (!boolSetting(UseToolTipsInMainEditor))
        return false;

    DebuggerEngine *engine = currentEngine();
    if (!engine || !engine->canDisplayTooltip())
        return false;

    DebuggerToolTipContext context;
    context.engineType = engine->objectName();
    context.fileName = editor->document()->filePath();
    context.position = pos;
    context.mousePosition = point;
    editor->convertPosition(pos, &context.line, &context.column);
    QString raw = cppExpressionAt(editor, context.position, &context.line, &context.column, &context.function);
    context.expression = fixCppExpression(raw);

    if (context.expression.isEmpty())
        return false;

    // Prefer a filter on an existing local variable if it can be found.
    if (const WatchData *localVariable = engine->watchHandler()->findCppLocalVariable(context.expression)) {
        context.expression = QLatin1String(localVariable->exp);
        if (context.expression.isEmpty())
            context.expression = localVariable->name;
        context.iname = localVariable->iname;
        showToolTip(context, engine);
        return true;
    }

    context.iname = "tooltip." + context.expression.toLatin1().toHex();

    if (engine->setToolTipExpression(editor, context))
        return true;

    // Other tooltip, close all in case mouse never entered the tooltip
    // and no leave was triggered.
    return false;
}


DebuggerToolTipContexts DebuggerToolTipManager::treeWidgetExpressions
    (DebuggerEngine *, const QString &fileName, const QString &function)
{
    DebuggerToolTipContexts rc;
    foreach (const QPointer<DebuggerToolTipWidget> &tw, d->m_tooltips) {
        if (tw && tw->context().matchesFrame(fileName, function))
            rc.push_back(tw->context());
    }
    return rc;
}

} // namespace Internal
} // namespace Debugger

#include "debuggertooltipmanager.moc"
