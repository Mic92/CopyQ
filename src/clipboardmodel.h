/*
    Copyright (c) 2009, Lukas Holecek <hluk@email.cz>

    This file is part of CopyQ.

    CopyQ is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    CopyQ is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with CopyQ.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef CLIPBOARDMODEL_H
#define CLIPBOARDMODEL_H

#include <QAbstractListModel>
#include <QStringList>
#include <QRegExp>
#include <QList>
#include <QHash>
#include <QMimeData>

class ClipboardItem;

static const QModelIndex empty_index;

class ClipboardModel : public QAbstractListModel
{
    Q_OBJECT

public:
    explicit ClipboardModel(QObject *parent = 0);

    // needs to be implemented
    int rowCount(const QModelIndex &parent = QModelIndex()) const;
    QVariant data(const QModelIndex &index, int role) const;

    // convinience
    QVariant data(int row) const;
    
    QMimeData *mimeData(int row) const;
    ClipboardItem *at(int row) const;

    // editing
    Qt::ItemFlags flags(const QModelIndex &index) const;
    bool setData(const QModelIndex &index, const QVariant &value,
                  int role = Qt::EditRole);
    bool insertRows(int position, int rows, const QModelIndex &index = QModelIndex());
    bool removeRows(int position, int rows, const QModelIndex &index = QModelIndex());
    void clear() {
        m_clipboardList.clear();
    }

    bool setData(const QModelIndex &index, QMimeData *value);
    bool append(ClipboardItem *item);

    void setMaxItems(int max);

    bool move(int pos, int newpos);

    /**
    @fn  moveItems
    @arg list items to move
    @arg key move items in given direction (Qt::Key_Down, Qt::Key_Up, Qt::Key_End, Qt::Key_Home)
    @return true if some item was moved to the top (item to clipboard), otherwise false
    */
    bool moveItems(QModelIndexList list, int key);

    // search
    const QRegExp *search() const { return &m_re; }
    void setSearch(const QRegExp *const re = NULL);
    void setSearch(int row);
    bool isFiltered(int i) const; // is item filtered out

    int getRowNumber(int row, bool cycle = false) const;

    ClipboardItem* get(int row) {
        return (row < rowCount()) ? m_clipboardList[row] : NULL;
    }

    const QStringList &formats() const { return m_formats; }
    void setFormats(const QString &list);
    void setFormat(int row, const QString &mimeType);
    void nextFormat(int row);
    void previousFormat(int row);

signals:
    /* unlike dataChanged, realDataChanged is not emitted
       if highlight or current format changes */
    void realDataChanged(const QModelIndex &topLeft,
                         const QModelIndex &bottomRight);

private:
    QList<ClipboardItem *> m_clipboardList;
    QRegExp m_re;
    int m_max;
    QStringList m_formats;
};

QDataStream &operator<<(QDataStream &stream, const ClipboardModel &model);
QDataStream &operator>>(QDataStream &stream, ClipboardModel &model);

#endif // CLIPBOARDMODEL_H
