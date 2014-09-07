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

#include "qmlprofilertimelinemodelproxy.h"
#include "qmlprofilermodelmanager.h"
#include "qmlprofilerdatamodel.h"
#include "abstracttimelinemodel_p.h"

#include <QCoreApplication>
#include <QVector>
#include <QHash>
#include <QUrl>
#include <QString>
#include <QStack>

#include <QDebug>

namespace QmlProfiler {
namespace Internal {

class RangeTimelineModel::RangeTimelineModelPrivate : public AbstractTimelineModelPrivate
{
public:
    // convenience functions
    void computeNestingContracted();
    void computeExpandedLevels();
    void findBindingLoops();

    QVector<QmlRangeEventStartInstance> data;
    QVector<int> expandedRowTypes;
    int contractedRows;
    bool seenPaintEvent;
private:
    Q_DECLARE_PUBLIC(RangeTimelineModel)
};

RangeTimelineModel::RangeTimelineModel(QmlDebug::RangeType rangeType, QObject *parent)
    : AbstractTimelineModel(new RangeTimelineModelPrivate, categoryLabel(rangeType),
                            QmlDebug::MaximumMessage, rangeType, parent)
{
    Q_D(RangeTimelineModel);
    d->seenPaintEvent = false;
    d->expandedRowTypes << -1;
    d->contractedRows = 1;
}

void RangeTimelineModel::clear()
{
    Q_D(RangeTimelineModel);
    d->expandedRowTypes.clear();
    d->expandedRowTypes << -1;
    d->contractedRows = 1;
    d->seenPaintEvent = false;
    d->data.clear();
    AbstractTimelineModel::clear();
}

void RangeTimelineModel::loadData()
{
    Q_D(RangeTimelineModel);
    clear();
    QmlProfilerDataModel *simpleModel = d->modelManager->qmlModel();
    if (simpleModel->isEmpty())
        return;

    // collect events
    const QVector<QmlProfilerDataModel::QmlEventData> &eventList = simpleModel->getEvents();
    const QVector<QmlProfilerDataModel::QmlEventTypeData> &typesList = simpleModel->getEventTypes();
    foreach (const QmlProfilerDataModel::QmlEventData &event, eventList) {
        const QmlProfilerDataModel::QmlEventTypeData &type = typesList[event.typeIndex];
        if (!accepted(type))
            continue;
        if (type.rangeType == QmlDebug::Painting)
            d->seenPaintEvent = true;

        // store starttime-based instance
        d->data.insert(insert(event.startTime, event.duration),
                       QmlRangeEventStartInstance(event.typeIndex));
        d->modelManager->modelProxyCountUpdated(d->modelId, count(), eventList.count() * 6);
    }

    d->modelManager->modelProxyCountUpdated(d->modelId, 2, 6);

    // compute range nesting
    computeNesting();

    // compute nestingLevel - nonexpanded
    d->computeNestingContracted();

    d->modelManager->modelProxyCountUpdated(d->modelId, 3, 6);

    // compute nestingLevel - expanded
    d->computeExpandedLevels();

    d->modelManager->modelProxyCountUpdated(d->modelId, 4, 6);

    d->findBindingLoops();

    d->modelManager->modelProxyCountUpdated(d->modelId, 5, 6);

    d->modelManager->modelProxyCountUpdated(d->modelId, 1, 1);
}

void RangeTimelineModel::RangeTimelineModelPrivate::computeNestingContracted()
{
    Q_Q(RangeTimelineModel);
    int i;
    int eventCount = q->count();

    int nestingLevels = QmlDebug::Constants::QML_MIN_LEVEL;
    contractedRows = nestingLevels + 1;
    QVector<qint64> nestingEndTimes;
    nestingEndTimes.fill(0, nestingLevels + 1);

    for (i = 0; i < eventCount; i++) {
        qint64 st = q->ranges[i].start;

        // per type
        if (nestingEndTimes[nestingLevels] > st) {
            if (++nestingLevels == nestingEndTimes.size())
                nestingEndTimes << 0;
            if (nestingLevels == contractedRows)
                ++contractedRows;
        } else {
            while (nestingLevels > QmlDebug::Constants::QML_MIN_LEVEL &&
                   nestingEndTimes[nestingLevels-1] <= st)
                nestingLevels--;
        }
        nestingEndTimes[nestingLevels] = st + q->ranges[i].duration;

        data[i].displayRowCollapsed = nestingLevels;
    }
}

void RangeTimelineModel::RangeTimelineModelPrivate::computeExpandedLevels()
{
    Q_Q(RangeTimelineModel);
    QHash<int, int> eventRow;
    int eventCount = q->count();
    for (int i = 0; i < eventCount; i++) {
        int eventId = data[i].eventId;
        if (!eventRow.contains(eventId)) {
            eventRow[eventId] = expandedRowTypes.size();
            expandedRowTypes << eventId;
        }
        data[i].displayRowExpanded = eventRow[eventId];
    }
}

void RangeTimelineModel::RangeTimelineModelPrivate::findBindingLoops()
{
    Q_Q(RangeTimelineModel);
    if (rangeType != QmlDebug::Binding && rangeType != QmlDebug::HandlingSignal)
        return;

    typedef QPair<int, int> CallStackEntry;
    QStack<CallStackEntry> callStack;

    for (int i = 0; i < q->count(); ++i) {
        const Range *potentialParent = callStack.isEmpty()
                ? 0 : &q->ranges[callStack.top().second];

        while (potentialParent
               && !(potentialParent->start + potentialParent->duration > q->ranges[i].start)) {
            callStack.pop();
            potentialParent = callStack.isEmpty() ? 0
                                                  : &q->ranges[callStack.top().second];
        }

        // check whether event is already in stack
        for (int ii = 0; ii < callStack.size(); ++ii) {
            if (callStack.at(ii).first == data[i].eventId) {
                data[i].bindingLoopHead = callStack.at(ii).second;
                break;
            }
        }


        CallStackEntry newEntry(data[i].eventId, i);
        callStack.push(newEntry);
    }

}

/////////////////// QML interface

int RangeTimelineModel::rowCount() const
{
    Q_D(const RangeTimelineModel);
    // special for paint events: show only when empty model or there's actual events
    if (d->rangeType == QmlDebug::Painting && !d->seenPaintEvent)
        return 0;
    if (d->expanded)
        return d->expandedRowTypes.size();
    else
        return d->contractedRows;
}

QString RangeTimelineModel::categoryLabel(int categoryIndex)
{
    switch (categoryIndex) {
    case 0: return QCoreApplication::translate("MainView", "Painting"); break;
    case 1: return QCoreApplication::translate("MainView", "Compiling"); break;
    case 2: return QCoreApplication::translate("MainView", "Creating"); break;
    case 3: return QCoreApplication::translate("MainView", "Binding"); break;
    case 4: return QCoreApplication::translate("MainView", "Handling Signal"); break;
    case 5: return QCoreApplication::translate("MainView", "JavaScript"); break;
    default: return QString();
    }
}

int RangeTimelineModel::row(int index) const
{
    Q_D(const RangeTimelineModel);
    if (d->expanded)
        return d->data[index].displayRowExpanded;
    else
        return d->data[index].displayRowCollapsed;
}

int RangeTimelineModel::eventId(int index) const
{
    Q_D(const RangeTimelineModel);
    return d->data[index].eventId;
}

int RangeTimelineModel::bindingLoopDest(int index) const
{
    Q_D(const RangeTimelineModel);
    return d->data[index].bindingLoopHead;
}

QColor RangeTimelineModel::color(int index) const
{
    return colorByEventId(index);
}

QVariantList RangeTimelineModel::labels() const
{
    Q_D(const RangeTimelineModel);
    QVariantList result;

    if (d->expanded) {
        const QVector<QmlProfilerDataModel::QmlEventTypeData> &types =
                d->modelManager->qmlModel()->getEventTypes();
        int eventCount = d->expandedRowTypes.count();
        for (int i = 1; i < eventCount; i++) { // Ignore the -1 for the first row
            QVariantMap element;
            int typeId = d->expandedRowTypes[i];
            element.insert(QLatin1String("displayName"), QVariant(types[typeId].displayName));
            element.insert(QLatin1String("description"), QVariant(types[typeId].data));
            element.insert(QLatin1String("id"), QVariant(typeId));
            result << element;
        }
    }

    return result;
}

QVariantMap RangeTimelineModel::details(int index) const
{
    Q_D(const RangeTimelineModel);
    QVariantMap result;
    int id = eventId(index);
    const QVector<QmlProfilerDataModel::QmlEventTypeData> &types =
            d->modelManager->qmlModel()->getEventTypes();

    result.insert(QStringLiteral("displayName"), categoryLabel(d->rangeType));
    result.insert(tr("Duration"), QmlProfilerBaseModel::formatTime(range(index).duration));

    result.insert(tr("Details"), types[id].data);
    result.insert(tr("Location"), types[id].displayName);
    return result;
}

QVariantMap RangeTimelineModel::location(int index) const
{
    Q_D(const RangeTimelineModel);
    QVariantMap result;
    int id = eventId(index);

    const QmlDebug::QmlEventLocation &location
            = d->modelManager->qmlModel()->getEventTypes().at(id).location;

    result.insert(QStringLiteral("file"), location.filename);
    result.insert(QStringLiteral("line"), location.line);
    result.insert(QStringLiteral("column"), location.column);

    return result;
}

int RangeTimelineModel::eventIdForTypeIndex(int typeIndex) const
{
    Q_D(const RangeTimelineModel);
    if (typeIndex < 0)
        return -1;
    const QmlProfilerDataModel::QmlEventTypeData &type =
            d->modelManager->qmlModel()->getEventTypes().at(typeIndex);
    if (type.message != d->message || type.rangeType != d->rangeType)
        return -1;
    return typeIndex;
}

int RangeTimelineModel::eventIdForLocation(const QString &filename, int line, int column) const
{
    Q_D(const RangeTimelineModel);
    // if this is called from v8 view, we don't have the column number, it will be -1
    const QVector<QmlProfilerDataModel::QmlEventTypeData> &types =
            d->modelManager->qmlModel()->getEventTypes();
    for (int i = 1; i < d->expandedRowTypes.size(); ++i) {
        int typeId = d->expandedRowTypes[i];
        const QmlProfilerDataModel::QmlEventTypeData &eventData = types[typeId];
        if (eventData.location.filename == filename &&
                eventData.location.line == line &&
                (column == -1 || eventData.location.column == column))
            return typeId;
    }
    return -1;
}



}
}
