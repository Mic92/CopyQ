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

#include "clipboardbrowser.h"
#include <QKeyEvent>
#include <QMenu>
#include <QMimeData>
#include <unistd.h> //usleep
#include <actiondialog.h>
#include "itemdelegate.h"
#include "clipboardmodel.h"
#include "clipboarditem.h"
#include "configurationmanager.h"
#include "client_server.h"

ClipboardBrowser::ClipboardBrowser(const QString &id, QWidget *parent) :
    QListView(parent), m_id(id), m_update(false), m_to_preload(0), m_menu(NULL)
{
    setLayoutMode(QListView::Batched);
    setBatchSize(10);

    timer_save.setSingleShot(true);
    connect( &timer_save, SIGNAL(timeout()),
             this, SLOT(saveItems()) );

    // delegate for rendering and editing items
    d = new ItemDelegate(this);
    setItemDelegate(d);

    // set new model
    m = new ClipboardModel();
    QItemSelectionModel *old_model = selectionModel();
    setModel(m);
    delete old_model;

    // context menu
    createContextMenu();

    connect( m, SIGNAL(rowsRemoved(QModelIndex,int,int)),
             d, SLOT(rowsRemoved(QModelIndex,int,int)) );
    connect( m, SIGNAL(rowsInserted(QModelIndex, int, int)),
             d, SLOT(rowsInserted(QModelIndex, int, int)) );
    connect( m, SIGNAL(rowsMoved(QModelIndex, int, int, QModelIndex, int)),
             d, SLOT(rowsMoved(QModelIndex, int, int, QModelIndex, int)) );
    connect( m, SIGNAL(dataChanged(QModelIndex,QModelIndex)),
             d, SLOT(dataChanged(QModelIndex,QModelIndex)));
    connect( m, SIGNAL(realDataChanged(QModelIndex,QModelIndex)),
             this, SLOT(realDataChanged(QModelIndex,QModelIndex)));

    connect( this, SIGNAL(doubleClicked(QModelIndex)),
            SLOT(moveToClipboard(QModelIndex)));

    // ScrollPerItem doesn't work well with hidden items
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);

    setAttribute(Qt::WA_MacShowFocusRect, 0);
}

ClipboardBrowser::~ClipboardBrowser()
{
    emit closeAllEditors();
}


void ClipboardBrowser::closeEditor(QEditor *editor)
{
    // check if file was modified before closing
    if ( editor->fileModified() )
        itemModified( editor->getText() );

    editor->disconnect(this);
    disconnect(editor);
    delete editor;
}

void ClipboardBrowser::contextMenuAction(QAction *act)
{
    QModelIndex ind;
    QVariant data = act->data();

    if ( data.isValid() ) {
        int action = data.toInt();
        switch(action) {
        case ActionToClipboard:
            moveToClipboard( currentIndex() );
            setCurrent(0);
            break;
        case ActionRemove:
            remove();
            break;
        case ActionEdit:
            ind = currentIndex();
            scrollTo(ind, PositionAtTop);
            emit requestShow();
            edit(ind);
            break;
        case ActionEditor:
            openEditor();
            break;
        case ActionAct:
            emit requestActionDialog(this, -1);
            break;
        case ActionCustom:
            emit requestActionDialog(this, -1, &commands[act->text()]);
            break;
        }
    }
}

void ClipboardBrowser::createContextMenu()
{
    QAction *act;
    QFont font;

    m_menu = new QMenu(this);

    act = m_menu->addAction( QIcon(":/images/clipboard.svg"),
                           tr("Move to &Clipboard") );
    font = act->font();
    font.setBold(true);
    act->setFont(font);
    act->setData( QVariant(ActionToClipboard) );
    act->setShortcuts(QKeySequence::Copy);

    act = m_menu->addAction( QIcon(":/images/remove.png"), tr("&Remove") );
    act->setShortcut( QString("Delete") );
    act->setData( QVariant(ActionRemove) );

    act = m_menu->addAction( QIcon(":/images/edit.svg"), tr("&Edit") );
    act->setShortcut( QString("F2") );
    act->setData( QVariant(ActionEdit) );

    act = m_menu->addAction( QIcon(":/images/edit.svg"),
                             tr("E&dit with editor") );
    act->setShortcut( QString("Ctrl+E") );
    act->setData( QVariant(ActionEditor) );

    act = m_menu->addAction( QIcon(":/images/action.svg"), tr("&Action...") );
    act->setShortcut( QString("F5") );
    act->setData( QVariant(ActionAct) );

    connect( m_menu, SIGNAL(triggered(QAction*)),
            this, SLOT(contextMenuAction(QAction*)) );
    connect( m_menu, SIGNAL(aboutToShow()),
             this, SLOT(updateContextMenu()) );
}

void ClipboardBrowser::updateContextMenu()
{
    int i, len;
    QString text = selectedText();
    QAction *act;
    QList<QAction *> actions = m_menu->actions();

    // remove old actions
    for( i = 0, len = actions.size(); i<len && !actions[i]->isSeparator(); ++i );
    for( ; i<len; ++i )
        m_menu->removeAction(actions[i]);

    // add custom commands to menu
    if ( !commands.isEmpty() ) {
        m_menu->addSeparator();
        QStringList keys = commands.keys();
        for( i=0, len=keys.size(); i<len; ++i ) {
            const QString &name = keys[i];
            const ConfigurationManager::Command *command = &commands[name];
            if ( command->re.indexIn(text) != -1 ) {
                act = m_menu->addAction(command->icon, name);
                act->setData( QVariant(ActionCustom) );
                if ( !command->shortcut.isEmpty() )
                    act->setShortcut( command->shortcut );
            }
        }
    }
}

void ClipboardBrowser::contextMenuEvent(QContextMenuEvent *event)
{
    if ( !selectedIndexes().isEmpty() )
        m_menu->exec( event->globalPos() );
}

void ClipboardBrowser::selectionChanged(const QItemSelection &a,
                                        const QItemSelection &b)
{
    QListView::selectionChanged(a, b);
    //updateContextMenu();
}

void ClipboardBrowser::openEditor()
{
    QEditor *editor = new QEditor(selectedText(), m_editor);

    connect( editor, SIGNAL(fileModified(const QString &)),
            this, SLOT(itemModified(const QString &)) );

    connect( editor, SIGNAL(closed(QEditor *)),
            this, SLOT(closeEditor(QEditor *)) );

    connect( this, SIGNAL(closeAllEditors()), editor, SLOT(close()) );

    if ( !editor->start() )
        closeEditor(editor);
}

void ClipboardBrowser::addItems(const QStringList &items)
{
    for(int i=items.count()-1; i>=0; --i) {
        add(items[i]);
    }
}

void ClipboardBrowser::itemModified(const QString &str)
{
    // add new item
    if ( !str.isEmpty() ) {
        add(str);
    }
}

void ClipboardBrowser::filterItems(const QString &str)
{
    if (m_last_filter == str)
        return;
    m_last_filter = str;

    // if search string empty: all items visible
    if ( str.isEmpty() ) {
        m->setSearch();
        emit hideSearch();
    } else {
        // if search string is a number N: highlight Nth item
        bool ok;
        int n = str.toInt(&ok);
        if (ok && n >= 0 && n < m->rowCount()) {
            m->setSearch(n);
            setCurrent(n);
            return;
        } else {
            QRegExp re(str);
            re.setCaseSensitivity(Qt::CaseInsensitive);
            m->setSearch(&re);
        }
    }

    // hide filtered items
    reset();
    int first = -1;
    for(int i = 0; i < m->rowCount(); ++i) {
        if ( m->isFiltered(i) )
            setRowHidden(i,true);
        else if (first == -1)
                first = i;
    }
    // select first visible
    setCurrentIndex( index(first) );
}

void ClipboardBrowser::moveToClipboard(const QModelIndex &ind)
{
    if ( ind.isValid() )
        moveToClipboard(ind.row());
}

void ClipboardBrowser::moveToClipboard(int i)
{
    m->move(i,0);
    updateClipboard();
    scrollTo( currentIndex() );
    update();
}

void ClipboardBrowser::newItem()
{
    // new text will allocate more space (lines) for editor
    add( QString(), false );
    selectionModel()->clearSelection();
    setCurrent(0);
    edit( index(0) );
}

void ClipboardBrowser::keyPressEvent(QKeyEvent *event)
{
    // if editing item, use default key action
    if ( state() == QAbstractItemView::EditingState ) {
        QListView::keyPressEvent(event);
    }
    // CTRL
    else if ( event->modifiers() == Qt::ControlModifier ) {
        int key = event->key();
        switch ( key ) {
        // move items
        case Qt::Key_Down:
        case Qt::Key_Up:
        case Qt::Key_End:
        case Qt::Key_Home:
            if ( m->moveItems(selectedIndexes(), key) )
                updateClipboard();
            scrollTo( currentIndex() );
            break;

        // cycle formats
        case Qt::Key_Left:
            m->previousFormat( currentIndex().row() );
            break;
        case Qt::Key_Right:
            m->nextFormat( currentIndex().row() );
            break;

        default:
            QListView::keyPressEvent(event);
            break;
        }
    }
    else {
        int key = event->key();
        switch ( key ) {
        // navigation
        case Qt::Key_Up:
        case Qt::Key_Down:
        case Qt::Key_PageDown:
        case Qt::Key_PageUp:
        case Qt::Key_Home:
        case Qt::Key_End:
            QListView::keyPressEvent(event);
            break;

        default:
            event->ignore();
            break;
        }
    }
}

void ClipboardBrowser::setCurrent(int row, bool cycle, bool selection)
{
    // direction
    int cur = currentIndex().row();
    int dir = cur < row ? 1 : -1;

    // select first visible
    int i = m->getRowNumber(row, cycle);
    cur = i;
    while ( isRowHidden(i) ) {
        i = m->getRowNumber(i+dir, cycle);
        if ( (!cycle && (i==0 || i==m->rowCount()-1)) || i == cur)
            break;
    }

    QModelIndex ind = index(i);
    if (selection) {
        QItemSelectionModel *sel = selectionModel();
        if ( sel->isSelected(ind) && sel->isSelected(currentIndex()) )
            sel->setCurrentIndex(currentIndex(),
                QItemSelectionModel::Deselect);
        sel->setCurrentIndex(ind,
            QItemSelectionModel::Select);
    }
    else
        setCurrentIndex(ind);

    scrollTo(ind); // ensure visible
}

ClipboardItem *ClipboardBrowser::at(int row) const
{
    return m->at(row);
}

void ClipboardBrowser::remove()
{
    QItemSelectionModel *sel = selectionModel();
    QModelIndexList list = sel->selectedIndexes();

    if ( !list.isEmpty() ) {
        int i;
        bool need_sync = false;

        do {
            i = list.at(0).row();
            if ( i == 0 )
                need_sync = true;
            m->removeRow(i);
            list = sel->selectedIndexes();
        } while( !list.isEmpty() );

        // select next
        setCurrent(i);

        if (need_sync)
            updateClipboard();
    }
}

bool ClipboardBrowser::add(const QString &txt, bool ignore_empty)
{
    QMimeData *data = new QMimeData;
    data->setText(txt);
    return add(data, ignore_empty);
}

bool ClipboardBrowser::add(QMimeData *data, bool ignore_empty)
{
    QStringList formats = data->formats();

    // ignore empty
    if ( ignore_empty && (formats.isEmpty() || (data->hasText() && data->text().isEmpty())) ) {
        delete data;
        return false;
    }

    // don't add if new data is same as first item
    if ( m->rowCount() ) {
        QMimeData *olddata = m->mimeData(0);
        QStringList oldformats = olddata->formats();
        if ( oldformats == formats &&
             olddata->data(oldformats.at(0)) == data->data(formats.at(0)) ) {
            delete data;
            return false;
        }
    }

    // create new item
    m->insertRow(0);
    QModelIndex ind = index(0);
    m->setData(ind, data);

    // leave the first item selected
    if ( selectionModel()->selectedIndexes().size() <= 1 &&
         currentIndex().row() <= 1 )
        setCurrent(0);

    // filter item
    if ( m->isFiltered(0) )
        setRowHidden(0,true);

    // list size limit
    if ( m->rowCount() > m_maxitems )
        m->removeRow( m->rowCount() - 1 );

    // preload item
    ++m_to_preload;
    preload();

    // save history after 2 minutes
    saveItems(120000);

    // automatic action
    foreach(const ConfigurationManager::Command *command, auto_commands) {
        if (command->re.indexIn(data->text()) != -1)
            emit requestActionDialog(this, 0, command);
    }

    return true;
}

bool ClipboardBrowser::add(ClipboardItem *item)
{
    if ( !m->insertRow(0) )
        return false;
    m->at(0)->setData( cloneData(*item->data()) );

    return true;
}

bool ClipboardBrowser::canAdd(const QMimeData &data) const
{
    return m_ignore.isEmpty() || !data.hasText() ||
            data.text().indexOf(m_ignore) == -1;
}

void ClipboardBrowser::loadSettings()
{
    ConfigurationManager *cm = ConfigurationManager::instance();
    setStyleSheet( cm->styleSheet() );
    d->setStyleSheet( cm->styleSheet() );

    // restore configuration
    m_editor = cm->value("editor").toString();
    d->setItemFormat( cm->value("format").toString());

    m_maxitems = cm->value("maxitems").toInt();
    m->setMaxItems(m_maxitems);
    m->setFormats( cm->value("priority").toString() );
    m_ignore.setPattern( cm->value("ignore").toString() );

    // commands
    commands = cm->commands();
    auto_commands.clear();
    foreach(const ConfigurationManager::Command &command, commands) {
        if (command.automatic)
            auto_commands.append(&command);
    }

    update();
}

void ClipboardBrowser::loadItems()
{
    timer_save.stop();

    // restore items
    m->clear();

    ConfigurationManager::instance()->loadItems(*m, m_id);
    setCurrentIndex( QModelIndex() );

    // preload all items
    m_to_preload = length();
    preload(0);
}

void ClipboardBrowser::saveItems(int msec)
{
    timer_save.stop();
    if (msec>0) {
        timer_save.start(msec);
        return;
    }

    ConfigurationManager::instance()->saveItems(*m, m_id);
}

void ClipboardBrowser::timerEvent(QTimerEvent *event)
{
    if ( event->timerId() == timer_preload.timerId() ) {
        timer_preload.stop();
        preload(0);
    } else {
        QListView::timerEvent(event);
    }
}

void ClipboardBrowser::preload(int msec)
{
    if ( this->isVisible() ) return;

    // performance:
    // preload each item
    // -- this will save some time when drawing the list for the fist time
    if (msec > 0) {
        timer_preload.start(msec, this);
    } else {
        int len = qMin(m_to_preload, length());
        for(int i=0; i<len; ++i) {
            d->preload( index(i) );
        }
        m_to_preload = -1;
    }
}

const QString ClipboardBrowser::selectedText() const
{
    QString result;
    QItemSelectionModel *sel = selectionModel();

    QModelIndexList list = sel->selectedIndexes();
    foreach(QModelIndex ind, list) {
        if ( !result.isEmpty() )
            result += QString('\n');
        result += itemText(ind);
    }

    return result;
}

QString ClipboardBrowser::itemText(int i) const
{
    if ( i >= m->rowCount() )
        return QString();
    return itemText( (i==-1) ? currentIndex() : index(i) );
}

QString ClipboardBrowser::itemText(QModelIndex ind) const
{
    return ind.isValid() ? ind.data(Qt::EditRole).toString() : QString();
}

const QMimeData *ClipboardBrowser::itemData(int i) const
{
    return m->mimeData( i>=0 ? i : currentIndex().row() );
}

void ClipboardBrowser::checkClipboard(QClipboard::Mode, QMimeData *data)
{
    add(data);
}

void ClipboardBrowser::updateClipboard()
{
    if ( m_update && m->rowCount() > 0 )
        emit changeClipboard(m->at(0));
}

void ClipboardBrowser::realDataChanged(const QModelIndex &a, const QModelIndex &)
{
    if ( a.row() == 0 )
        updateClipboard();
}
