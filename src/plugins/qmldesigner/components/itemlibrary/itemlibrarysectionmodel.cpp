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

#include "itemlibrarysectionmodel.h"

#include "itemlibraryitem.h"

namespace QmlDesigner {

ItemLibrarySectionModel::ItemLibrarySectionModel(QObject *parent) :
    QAbstractListModel(parent)
{
    addRoleNames();
}

ItemLibrarySectionModel::~ItemLibrarySectionModel()
{
    clearItems();
}

int ItemLibrarySectionModel::rowCount(const QModelIndex &) const
{
    return m_itemList.count();
}

QVariant ItemLibrarySectionModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() + 1 > m_itemList.count()) {
        qDebug() << Q_FUNC_INFO << "invalid index requested";
        return QVariant();
    }

    if (m_roleNames.contains(role)) {
        QVariant value = m_itemList.at(index.row())->property(m_roleNames.value(role));

        if (ItemLibrarySectionModel* model = qobject_cast<ItemLibrarySectionModel *>(value.value<QObject*>()))
            return QVariant::fromValue(model);

        return m_itemList.at(index.row())->property(m_roleNames.value(role));
    }

    qWarning() << Q_FUNC_INFO << "invalid role requested";

    return QVariant();
}

QHash<int, QByteArray> ItemLibrarySectionModel::roleNames() const
{
    return m_roleNames;
}

void ItemLibrarySectionModel::clearItems()
{
    beginResetModel();
    endResetModel();
}

void ItemLibrarySectionModel::addItem(ItemLibraryItem *element)
{
    m_itemList.append(element);

    element->setVisible(true);
}

const QList<ItemLibraryItem *> &ItemLibrarySectionModel::items() const
{
    return m_itemList;
}

void ItemLibrarySectionModel::sortItems()
{
    auto itemSort = [](ItemLibraryItem *first, ItemLibraryItem *second) {
        return QString::localeAwareCompare(first->itemName(), second->itemName()) < 1;
    };

    std::sort(m_itemList.begin(), m_itemList.end(), itemSort);
}

void ItemLibrarySectionModel::resetModel()
{
    beginResetModel();
    endResetModel();
}

void ItemLibrarySectionModel::addRoleNames()
{
    int role = 0;
    for (int propertyIndex = 0; propertyIndex < ItemLibraryItem::staticMetaObject.propertyCount(); ++propertyIndex) {
        QMetaProperty property = ItemLibraryItem::staticMetaObject.property(propertyIndex);
        m_roleNames.insert(role, property.name());
        ++role;
    }
}

} // namespace QmlDesigner
