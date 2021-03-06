/*
    Copyright (c) 2013, Lukas Holecek <hluk@email.cz>

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

#include "clipboardmonitor.h"

#include "common/client_server.h"
#include "item/clipboarditem.h"
#include "platform/platformnativeinterface.h"

#include <QApplication>
#include <QMimeData>
#include <QTimer>

#ifdef COPYQ_WS_X11
#  include "platform/x11/x11platform.h"
#endif

namespace {

void setClipboardData(QMimeData *data, QClipboard::Mode mode)
{
    Q_ASSERT( isMainThread() );
    Q_ASSERT(data != NULL);
    COPYQ_LOG( QString("Setting %1.").arg(mode == QClipboard::Clipboard ? "clipboard"
                                                                        : "selection") );
    QApplication::clipboard()->setMimeData(data, mode);
}

} // namespace

#ifdef COPYQ_WS_X11
class PrivateX11 {
public:
    PrivateX11()
        : m_dsp()
        , m_timer()
        , m_syncTimer()
        , m_syncData(NULL)
        , m_syncTo(QClipboard::Clipboard)
    {
        m_timer.setSingleShot(true);
        m_timer.setInterval(100);
        m_syncTimer.setSingleShot(true);
        m_syncTimer.setInterval(100);
    }

    ~PrivateX11()
    {
        delete m_syncData;
    }

    bool waitForKeyRelease()
    {
        if (m_timer.isActive())
            return true;

        if ( m_dsp.isSelecting() ) {
            m_timer.start();
            return true;
        }

        return false;
    }

    const QTimer &timer() const
    {
        return m_timer;
    }

    const QTimer &syncTimer() const
    {
        return m_syncTimer;
    }

    void synchronizeNone()
    {
        Q_ASSERT( !isSynchronizing() );
        delete m_syncData;
        m_syncData = NULL;
        m_syncTimer.stop();
    }

    void synchronize(QMimeData *data, QClipboard::Mode modeSyncTo)
    {
        delete m_syncData;
        m_syncData = cloneData(*data);
        m_syncTo = modeSyncTo;
        m_syncTimer.start();
    }

    void synchronize()
    {
        if (m_syncData == NULL || m_syncTimer.isActive())
            return;

        if (m_syncTo == QClipboard::Selection && waitForKeyRelease()) {
            m_syncTimer.start();
            return;
        }

        Q_ASSERT( isSynchronizing() );
        setClipboardData(m_syncData, m_syncTo);
        m_syncData = NULL;
    }

    bool isSynchronizing()
    {
        return !m_syncTimer.isActive() && m_syncData != NULL;
    }

private:
    X11Platform m_dsp;
    QTimer m_timer;
    QTimer m_syncTimer;
    QMimeData *m_syncData;
    QClipboard::Mode m_syncTo;
};
#endif

ClipboardMonitor::ClipboardMonitor(int &argc, char **argv)
    : QObject()
    , App(new QApplication(argc, argv))
    , m_formats()
    , m_newdata()
#ifdef COPYQ_WS_X11
    , m_copyclip(false)
    , m_checksel(false)
    , m_copysel(false)
#endif
    , m_socket( new QLocalSocket(this) )
    , m_updateTimer( new QTimer(this) )
    , m_needCheckClipboard(false)
#ifdef COPYQ_WS_X11
    , m_needCheckSelection(false)
    , m_x11(new PrivateX11)
#endif
{
    connect( m_socket, SIGNAL(readyRead()),
             this, SLOT(readyRead()), Qt::DirectConnection );
    connect( m_socket, SIGNAL(disconnected()),
             QApplication::instance(), SLOT(quit()) );

    QStringList args = QCoreApplication::instance()->arguments();
    Q_ASSERT(args.size() == 3);

    const QString &serverName = args[2];
    COPYQ_LOG( QString("Connecting to server \"%1\".").arg(serverName) );
    m_socket->connectToServer(serverName);
    if ( !m_socket->waitForConnected(2000) ) {
        log( tr("Cannot connect to server!"), LogError );
        exit(1);
    }
    COPYQ_LOG("Connected to server.");

    m_updateTimer->setSingleShot(true);
    m_updateTimer->setInterval(300);
    connect( m_updateTimer, SIGNAL(timeout()),
             this, SLOT(updateTimeout()));

#ifdef COPYQ_WS_X11
    connect( &m_x11->timer(), SIGNAL(timeout()),
             this, SLOT(updateSelection()) );
    connect( &m_x11->syncTimer(), SIGNAL(timeout()),
             this, SLOT(synchronize()) );
#endif
}

ClipboardMonitor::~ClipboardMonitor()
{
#ifdef COPYQ_WS_X11
    delete m_x11;
#endif
}

#ifdef COPYQ_WS_X11
bool ClipboardMonitor::updateSelection(bool check)
{
    // Wait while selection is incomplete, i.e. mouse button or
    // shift key is pressed.
    if ( m_x11->waitForKeyRelease() )
        return false;

    if (check)
        checkClipboard(QClipboard::Selection);
    return true;
}
#endif

#ifdef COPYQ_WS_X11
void ClipboardMonitor::synchronize()
{
    m_x11->synchronize();
}
#endif /* !COPYQ_WS_X11 */

void ClipboardMonitor::checkClipboard(QClipboard::Mode mode)
{
#ifdef COPYQ_WS_X11
    if ( m_x11->isSynchronizing() )
        return;
    m_x11->synchronizeNone();
#endif

    // Check clipboard after interval because someone is updating it very quickly.
    bool needToWait = m_updateTimer->isActive();
    if (mode == QClipboard::Clipboard)
        m_needCheckClipboard = needToWait;
#ifdef COPYQ_WS_X11
    else if (mode == QClipboard::Selection)
        m_needCheckSelection = needToWait;
#endif

    m_updateTimer->start();
    if (needToWait)
        return;

    COPYQ_LOG( QString("Checking for new %1 content.")
               .arg(mode == QClipboard::Clipboard ? "clipboard" : "selection") );
#ifdef COPYQ_WS_X11
    if (mode == QClipboard::Clipboard) {
        if ( QApplication::clipboard()->ownsClipboard() )
            return;
    } else if (mode == QClipboard::Selection) {
        if ( (!m_checksel && !m_copysel) ||
             QApplication::clipboard()->ownsSelection() ||
             !updateSelection(false) )
        {
            return;
        }
    } else {
        return;
    }
#else /* !COPYQ_WS_X11 */
    // check if clipboard data are needed
    if (mode != QClipboard::Clipboard || QApplication::clipboard()->ownsClipboard())
        return;
#endif

    // get clipboard data
    const QMimeData *data = clipboardData(mode);

    // data retrieved?
    if (!data) {
        log( tr("Cannot access clipboard data!"), LogError );
        return;
    }

    // clone only mime types defined by user
    QMimeData *data2 = cloneData(*data, &m_formats);

    // add window title of clipboard owner
    PlatformPtr platform = createPlatformNativeInterface();
    data2->setData( QString(mimeWindowTitle),
                    platform->getWindowTitle(platform->getCurrentWindow()).toUtf8() );

#ifdef COPYQ_WS_X11
    if (mode == QClipboard::Clipboard) {
        if (m_copyclip)
            m_x11->synchronize(data2, QClipboard::Selection);
        clipboardChanged(mode, data2);
    } else {
        if (m_copysel)
            m_x11->synchronize(data2, QClipboard::Clipboard);
        if (m_checksel)
            clipboardChanged(mode, data2);
        else
            delete data2;
    }
#else /* !COPYQ_WS_X11 */
    clipboardChanged(mode, data2);
#endif
}

void ClipboardMonitor::clipboardChanged(QClipboard::Mode, QMimeData *data)
{
    ClipboardItem item;

    item.setData(data);

    // send clipboard item
    QByteArray msg;
    QDataStream out(&msg, QIODevice::WriteOnly);
    out << item;
    writeMessage(m_socket, msg);
}

void ClipboardMonitor::updateTimeout()
{
    if (m_needCheckClipboard) {
        checkClipboard(QClipboard::Clipboard);
#ifdef COPYQ_WS_X11
    } else if (m_needCheckSelection) {
        checkClipboard(QClipboard::Selection);
#endif
    } else if (m_newdata) {
        updateClipboard();
    }
}

void ClipboardMonitor::readyRead()
{
    m_socket->blockSignals(true);

    while ( m_socket->bytesAvailable() > 0 ) {
        QByteArray msg;
        if( !readMessage(m_socket, &msg) ) {
            log( tr("Cannot read message from server!"), LogError );
            return;
        }

        ClipboardItem item;
        QDataStream in(&msg, QIODevice::ReadOnly);
        in >> item;

        /* Does server send settings for monitor? */
        QByteArray settings_data = item.data()->data("application/x-copyq-settings");
        if ( !settings_data.isEmpty() ) {

            QDataStream settings_in(settings_data);
            QVariantMap settings;
            settings_in >> settings;

#ifdef COPYQ_LOG_DEBUG
            {
                COPYQ_LOG("Loading configuration:");
                foreach (const QString &key, settings.keys()) {
                    QVariant val = settings[key];
                    const QString str = val.canConvert<QStringList>() ? val.toStringList().join(",")
                                                                      : val.toString();
                    COPYQ_LOG( QString("    %1=%2").arg(key).arg(str) );
                }
            }
#endif

            if ( settings.contains("formats") )
                m_formats = settings["formats"].toStringList();
#ifdef COPYQ_WS_X11
            if ( settings.contains("copy_clipboard") )
                m_copyclip = settings["copy_clipboard"].toBool();
            if ( settings.contains("copy_selection") )
                m_copysel = settings["copy_selection"].toBool();
            if ( settings.contains("check_selection") )
                m_checksel = settings["check_selection"].toBool();
#endif

            connect( QApplication::clipboard(), SIGNAL(changed(QClipboard::Mode)),
                     this, SLOT(checkClipboard(QClipboard::Mode)) );

#ifdef COPYQ_WS_X11
            checkClipboard(QClipboard::Selection);
#endif
            checkClipboard(QClipboard::Clipboard);

            COPYQ_LOG("Configured");
        } else {
            updateClipboard( cloneData(*item.data()) );
        }
    }

    m_socket->blockSignals(false);
}

void ClipboardMonitor::updateClipboard(QMimeData *data)
{
    if (data != NULL)
        m_newdata.reset(data);
    if ( m_updateTimer->isActive() )
        return;

    COPYQ_LOG("Updating clipboard");

#ifdef COPYQ_WS_X11
    setClipboardData(cloneData(*m_newdata), QClipboard::Selection);
    m_needCheckSelection = false;
#endif
    setClipboardData(m_newdata.take(), QClipboard::Clipboard);
    m_needCheckClipboard = false;

    m_newdata.reset();

    m_updateTimer->start();
}

