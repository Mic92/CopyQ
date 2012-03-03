#include "configurationmanager.h"
#include "ui_configurationmanager.h"
#include "clipboardmodel.h"
#include "shortcutdialog.h"
#include <QFile>
#include <QtGui/QDesktopWidget>
#include <QMessageBox>

#ifdef Q_WS_WIN
#define DEFAULT_EDITOR "notepad %1"
#else
#define DEFAULT_EDITOR "gedit %1"
#endif

struct _Option {
    _Option() : m_property_name(NULL), m_obj(NULL) {}
    _Option(const QVariant &default_value, const char *property_name = NULL, QObject *obj = NULL) :
        m_default_value(default_value), m_property_name(property_name), m_obj(obj)
    {
        reset();
    }

    QVariant value() const
    {
        return m_obj ? m_obj->property(m_property_name) : m_value;
    }

    void setValue(const QVariant &value)
    {
        if (m_obj)
            m_obj->setProperty(m_property_name, value);
        else
            m_value = value;
    }

    void reset()
    {
        setValue(m_default_value);
    }

    /* default value and also type (int, float, boolean, QString) */
    QVariant m_default_value, m_value;
    const char *m_property_name;
    QObject *m_obj;
};

inline bool readCssFile(QIODevice &device, QSettings::SettingsMap &map)
{
    map.insert( "css", device.readAll() );
    return true;
}

inline bool writeCssFile(QIODevice &device, const QSettings::SettingsMap &map)
{
    device.write( map["css"].toString().toLocal8Bit() );
    return true;
}

// singleton
ConfigurationManager* ConfigurationManager::m_Instance = 0;

ConfigurationManager::ConfigurationManager(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::ConfigurationManager)
{
    ui->setupUi(this);
    ui->tableCommands->horizontalHeader()->setResizeMode(QHeaderView::ResizeToContents);

    /* datafile for items */
    // do not use registry in windows
    QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                       QCoreApplication::organizationName(),
                       QCoreApplication::applicationName());

    /* options */
    m_options.insert( "maxitems",
                      Option(200, "value", ui->spinBoxItems) );
    m_options.insert( "tray_items",
                      Option(5, "value", ui->spinBoxTrayItems) );
    m_options.insert( "ignore",
                      Option("^\\s*$", "text", ui->lineEditIgnore) );
    m_options.insert( "priority",
                      Option("image/bmp\nimage/x-inkscape-svg-compressed\ntext/plain\ntext/html",
                             "plainText", ui->textEditFormats) );
    // TODO: get default editor from environment variable EDITOR
    m_options.insert( "editor",
                      Option(DEFAULT_EDITOR, "text", ui->lineEditEditor) );
    m_options.insert( "format",
                      Option("<span id=\"item\">%1</span>",
                             "plainText", ui->plainTextEdit_html) );
    m_options.insert( "check_selection",
                      Option(true, "checked", ui->checkBoxClip) );
    m_options.insert( "check_clipboard",
                      Option(true, "checked", ui->checkBoxSel) );
    m_options.insert( "copy_clipboard",
                      Option(true, "checked", ui->checkBoxCopyClip) );
    m_options.insert( "copy_selection",
                      Option(true, "checked", ui->checkBoxCopySel) );
    m_options.insert( "confirm_exit",
                      Option(true, "checked", ui->checkBoxConfirmExit) );
    m_options.insert( "tabs",
                      Option(QStringList()) );
    m_options.insert( "toggle_shortcut",
                      Option("(No Shortcut)", "text", ui->pushButton));
    m_options.insert( "menu_shortcut",
                      Option("(No Shortcut)", "text", ui->pushButton_2));

    m_datfilename = settings.fileName();
    m_datfilename.replace( QRegExp(".ini$"), QString("_tab_") );

    // read style sheet from configuration
    cssFormat = QSettings::registerFormat("css", readCssFile, writeCssFile);
    readStyleSheet();

    connect(this, SIGNAL(finished(int)), SLOT(onFinished(int)));

    loadSettings();
}

ConfigurationManager::~ConfigurationManager()
{
    delete ui;
}

void ConfigurationManager::loadItems(ClipboardModel &model, const QString &id)
{
    QString part( id.toLocal8Bit().toBase64() );
    part.replace( QChar('/'), QString('-') );
    QFile file( m_datfilename + part + QString(".dat") );
    file.open(QIODevice::ReadOnly);
    QDataStream in(&file);
    in >> model;
}

void ConfigurationManager::saveItems(const ClipboardModel &model, const QString &id)
{
    QString part( id.toLocal8Bit().toBase64() );
    part.replace( QChar('/'), QString('-') );
    QFile file( m_datfilename + part + QString(".dat") );
    file.open(QIODevice::WriteOnly);
    QDataStream out(&file);
    out << model;
}

void ConfigurationManager::removeItems(const QString &id)
{
    QString part( id.toLocal8Bit().toBase64() );
    part.replace( QChar('/'), QString('-') );
    QFile::remove( m_datfilename + part + QString(".dat") );
}

void ConfigurationManager::readStyleSheet()
{
    QSettings cssSettings( cssFormat, QSettings::UserScope,
                           QCoreApplication::organizationName(),
                           QCoreApplication::applicationName() );

    QString css = cssSettings.value("css").toString();

    if ( !css.isEmpty() ) {
        setStyleSheet(css);
        ui->plainTextEdit_css->setPlainText(css);
    } else {
        setStyleSheet( ui->plainTextEdit_css->toPlainText() );
    }
}

void ConfigurationManager::writeStyleSheet()
{
    QSettings cssSettings( cssFormat, QSettings::UserScope,
                           QCoreApplication::organizationName(),
                           QCoreApplication::applicationName() );

    cssSettings.setValue( "css", styleSheet() );
}

QByteArray ConfigurationManager::windowGeometry(const QString &widget_name, const QByteArray &geometry)
{
    QSettings settings;

    if ( geometry.isEmpty() ) {
        return settings.value("Options/"+widget_name+"_geometry").toByteArray();
    } else {
        settings.setValue("Options/"+widget_name+"_geometry", geometry);
        return geometry;
    }
}

QVariant ConfigurationManager::value(const QString &name) const
{
    return m_options[name].value();
}

void ConfigurationManager::setValue(const QString &name, const QVariant &value)
{
    m_options[name].setValue(value);
}

void ConfigurationManager::loadSettings()
{
    QSettings settings;

    QVariant value;

    settings.beginGroup("Options");
    foreach( const QString &key, m_options.keys() ) {
        m_options[key].setValue( settings.value(key) );
    }
    settings.endGroup();

    QTableWidget *table = ui->tableCommands;
    // clear table
    while( table->rowCount()>0 )
        table->removeRow(0);

    int size = settings.beginReadArray("Commands");
    for(int i=0; i<size; ++i)
    {
        settings.setArrayIndex(i);

        int columns = table->columnCount();
        int row = table->rowCount();

        table->insertRow(row);

        for (int col=0; col < columns; ++col) {
            QTableWidgetItem *column = table->horizontalHeaderItem(col);

            QTableWidgetItem *item = new QTableWidgetItem;
            value = settings.value(column->text());
            if( column->text() == tr("Enable") ||
                column->text() == tr("Input") ||
                column->text() == tr("Output") ||
                column->text() == tr("Wait") ||
                column->text() == tr("Automatic") ) {
                item->setCheckState(value.toBool() ? Qt::Checked : Qt::Unchecked);
            } else {
                if ( value.type() == QVariant::String )
                    item->setText( value.toString() );
                else if ( column->text() == tr("Separator") )
                    item->setText("\\n");
            }
            item->setToolTip( column->toolTip() );
            table->setItem(row, col, item);
        }
    }
    settings.endArray();
    ui->tableCommands->indexWidget( table->model()->index(0,0) );
}

ConfigurationManager::Commands ConfigurationManager::commands() const
{
    Commands cmds;

    QTableWidget *table = ui->tableCommands;
    int columns = table->columnCount();
    int rows = table->rowCount();

    for (int row=0; row < rows; ++row) {
        Command cmd;
        QString name;
        bool enabled = true;
        for (int col=0; col < columns; ++col) {
            QTableWidgetItem *column = table->horizontalHeaderItem(col);
            QTableWidgetItem *item = table->item(row, col);

            if ( column->text() == tr("Enable") ) {
                if ( item->checkState() == Qt::Unchecked ) {
                    enabled = false;
                    break;
                }
            } else if ( column->text() == tr("Name") ) {
                name = item->text();
            } else if ( column->text() == tr("Command") ) {
                cmd.cmd = item->text();
            } else if ( column->text() == tr("Input") ) {
                cmd.input = item->checkState() == Qt::Checked;
            } else if ( column->text() == tr("Output") ) {
                cmd.output = item->checkState() == Qt::Checked;
            } else if ( column->text() == tr("Separator") ) {
                cmd.sep = item->text();
            } else if ( column->text() == tr("Match") ) {
                cmd.re = QRegExp( item->text() );
            } else if ( column->text() == tr("Wait") ) {
                cmd.wait = item->checkState() == Qt::Checked;
            } else if ( column->text() == tr("Automatic") ) {
                cmd.automatic = item->checkState() == Qt::Checked;
            } else if ( column->text() == tr("Icon") ) {
                cmd.icon.addFile( item->text() );
            } else if ( column->text() == tr("Shortcut") ) {
                cmd.shortcut = item->text();
            }
        }
        if (enabled) {
            cmds[name] = cmd;
        }
    }

    return cmds;
}

void ConfigurationManager::saveSettings()
{
    QSettings settings;

    settings.beginGroup("Options");
    foreach( const QString &key, m_options.keys() ) {
        settings.setValue( key, m_options[key].value() );
    }
    settings.endGroup();

    QTableWidget *table = ui->tableCommands;
    int columns = table->columnCount();
    int rows = table->rowCount();

    settings.beginWriteArray("Commands");
    settings.remove("");
    for (int row=0; row < rows; ++row) {
        settings.setArrayIndex(row);
        for (int col=0; col < columns; ++col) {
            QTableWidgetItem *column = table->horizontalHeaderItem(col);
            QTableWidgetItem *item = table->item(row, col);

            if ( column->text() == tr("Enable") ||
                 column->text() == tr("Input") ||
                 column->text() == tr("Output") ||
                 column->text() == tr("Wait") ||
                 column->text() == tr("Automatic") ) {
                settings.setValue( column->text(), item->checkState() == Qt::Checked );
            } else {
                settings.setValue( column->text(), item->text() );
            }
        }
    }
    settings.endArray();

    writeStyleSheet();
}

void ConfigurationManager::on_buttonBox_clicked(QAbstractButton* button)
{
    int answer;

    switch( ui->buttonBox->buttonRole(button) ) {
    case QDialogButtonBox::ApplyRole:
        apply();
        break;
    case QDialogButtonBox::AcceptRole:
        accept();
        break;
    case QDialogButtonBox::RejectRole:
        reject();
        break;
    case QDialogButtonBox::ResetRole:
        // ask before resetting values
        answer = QMessageBox::question(
                    this,
                    tr("Reset preferences?"),
                    tr("This action will reset all your preferences (in all tabs) to default values.<br /><br />"
                       "Do you really want to <strong>reset all preferences</strong>?"),
                    QMessageBox::Yes | QMessageBox::No,
                    QMessageBox::Yes);
        if (answer == QMessageBox::Yes) {
            foreach( const QString &key, m_options.keys() ) {
                m_options[key].reset();
            }
        }
        break;
    default:
        return;
    }
}

void ConfigurationManager::addCommand(const QString &name, const Command *cmd, bool enable)
{
    QTableWidget *table = ui->tableCommands;

    int columns = table->columnCount();
    int row = table->rowCount();

    table->insertRow(row);

    for (int col=0; col < columns; ++col) {
        QTableWidgetItem *column = table->horizontalHeaderItem(col);

        QTableWidgetItem *item = new QTableWidgetItem;
        if ( column->text() == tr("Enable") ) {
            item->setCheckState(enable ? Qt::Checked : Qt::Unchecked);
            item->setFlags( item->flags() & ~Qt::ItemIsEditable );
        } else if ( column->text() == tr("Name") ) {
            item->setText(name);
        } else if ( column->text() == tr("Command") ) {
            item->setText(cmd->cmd);
        } else if ( column->text() == tr("Input") ) {
            item->setCheckState(cmd->input ? Qt::Checked : Qt::Unchecked);
        } else if ( column->text() == tr("Output") ) {
            item->setCheckState(cmd->output ? Qt::Checked : Qt::Unchecked);
        } else if ( column->text() == tr("Separator") ) {
            item->setText(cmd->sep);
        } else if ( column->text() == tr("Match") ) {
            item->setText(cmd->re.pattern());
        } else if ( column->text() == tr("Wait") ) {
            item->setCheckState(cmd->wait ? Qt::Checked : Qt::Unchecked);
        } else if ( column->text() == tr("Icon") ) {
            item->setText( cmd->icon.name() );
        } else if ( column->text() == tr("Shortcut") ) {
            item->setText(cmd->shortcut);
        } else if ( column->text() == tr("Automatic") ) {
            item->setCheckState(cmd->automatic ? Qt::Checked : Qt::Unchecked);
        }
        item->setToolTip( column->toolTip() );
        table->setItem(row, col, item);
    }

    saveSettings();
    if (enable) {
        emit configurationChanged();
    }
}

void ConfigurationManager::apply()
{
    setStyleSheet( ui->plainTextEdit_css->toPlainText() );
    emit configurationChanged();
    saveSettings();
}

void ConfigurationManager::on_pushButtoAdd_clicked()
{
    Command cmd;
    cmd.input = cmd.output = cmd.wait = cmd.automatic = false;
    cmd.sep = QString('\n');
    addCommand(QString(), &cmd);

    QTableWidget *table = ui->tableCommands;
    table->selectRow( table->rowCount()-1 );
}


void ConfigurationManager::on_pushButtonRemove_clicked()
{
    const QItemSelectionModel *sel = ui->tableCommands->selectionModel();

    // remove selected rows
    QModelIndexList rows = sel->selectedRows();
    while ( !rows.isEmpty() ) {
        ui->tableCommands->removeRow( rows.first().row() );
        rows = sel->selectedRows();
    }
}

void ConfigurationManager::on_tableCommands_itemSelectionChanged()
{
    // enable Remove button only if row is selected
    QItemSelectionModel *sel = ui->tableCommands->selectionModel();
    bool enabled = sel->selectedRows().count() != 0;
    ui->pushButtonRemove->setEnabled( enabled );
    ui->pushButtonUp->setEnabled( enabled );
    ui->pushButtonDown->setEnabled( enabled );
}

void ConfigurationManager::showEvent(QShowEvent *e)
{
    QDialog::showEvent(e);

    /* try to resize the dialog so that vertical scrollbar in the about
     * document is hidden
     */
    restoreGeometry( windowGeometry(objectName()) );
}

void ConfigurationManager::onFinished(int result)
{
    windowGeometry( objectName(), saveGeometry() );
    if (result == QDialog::Accepted)
        apply();
    else
        loadSettings();
}

void ConfigurationManager::on_pushButtonUp_clicked()
{
    QTableWidget *table = ui->tableCommands;
    QItemSelectionModel *sel = table->selectionModel();

    int columns = table->columnCount();

    QList<int> rows;
    foreach ( QModelIndex ind, sel->selectedRows() ) {
        rows << ind.row();
    }
    qSort( rows.begin(), rows.end() );

    foreach ( int row, rows ) {
        if (row == 0) break;
        for ( int col=0; col<columns; ++col ) {
            QTableWidgetItem *item1 = table->takeItem(row, col);
            QTableWidgetItem *item2 = table->takeItem(row-1, col);
            table->setItem(row, col, item2);
            table->setItem(row-1, col, item1);
            table->selectionModel()->select( table->model()->index(row, col),
                                             QItemSelectionModel::Deselect );
            table->selectionModel()->select( table->model()->index(row-1, col),
                                             QItemSelectionModel::Select );
        }
    }
}

void ConfigurationManager::on_pushButtonDown_clicked()
{
    QTableWidget *table = ui->tableCommands;
    QItemSelectionModel *sel = table->selectionModel();

    int columns = table->columnCount();

    QList<int> rows;
    foreach ( QModelIndex ind, sel->selectedRows() ) {
        rows << ind.row();
    }
    qSort( rows.begin(), rows.end(), qGreater<int>() );

    foreach ( int row, rows ) {
        if ( row == table->rowCount()-1 ) break;
        for ( int col=0; col<columns; ++col ) {
            QTableWidgetItem *item1 = table->takeItem(row, col);
            QTableWidgetItem *item2 = table->takeItem(row+1, col);
            table->setItem(row, col, item2);
            table->setItem(row+1, col, item1);
            table->selectionModel()->select( table->model()->index(row, col),
                                             QItemSelectionModel::Deselect );
            table->selectionModel()->select( table->model()->index(row+1, col),
                                             QItemSelectionModel::Select );
        }
    }
}

void ConfigurationManager::getKey(QPushButton *button)
{
    ShortcutDialog *dialog = new ShortcutDialog(this);
    if (dialog->exec() == QDialog::Accepted) {
        QKeySequence shortcut = dialog->shortcut();
        QString text;
        if (shortcut.isEmpty())
            text = tr("(No Shortcut)");
        else
            text = shortcut.toString(QKeySequence::NativeText);
        button->setText(text);
    }
}

void ConfigurationManager::on_pushButton_clicked()
{
    getKey(ui->pushButton);
}

void ConfigurationManager::on_pushButton_2_clicked()
{
    getKey(ui->pushButton_2);
}
