#include "function_editor.h"
#include "custom_function.h"
#include "plotwidget.h"
#include <QDebug>
#include <QMessageBox>
#include <QFont>
#include <QDomDocument>
#include <QDomElement>
#include <QFontDatabase>
#include <QFile>
#include <QMenu>
#include <QAction>
#include <QDir>
#include <QToolTip>
#include <QFileDialog>
#include <QFileInfo>
#include <QSettings>
#include <QByteArray>
#include <QInputDialog>
#include <QDragEnterEvent>
#include <QMouseEvent>
#include <QEnterEvent>
#include <QMimeData>
#include <QTableWidgetItem>
#include <QTimer>
#include <QListWidget>
#include <QListWidgetItem>
#include <QSyntaxHighlighter>
#include <QHeaderView>

#include <QGraphicsDropShadowEffect>
#include <QFontDatabase>

#include "QLuaHighlighter"

#include "lua_custom_function.h"
#include "PlotJuggler/svg_util.h"
#include "ui_function_editor_help.h"
#include "stylesheet.h"

void FunctionEditorWidget::on_stylesheetChanged(QString theme)
{
  ui->pushButtonDeleteCurves->setIcon(LoadSvg(":/resources/svg/trash.svg", theme));
  ui->buttonLoadFunctions->setIcon(LoadSvg(":/resources/svg/import.svg", theme));
  ui->buttonSaveFunctions->setIcon(LoadSvg(":/resources/svg/export.svg", theme));
  ui->buttonSaveCurrent->setIcon(LoadSvg(":/resources/svg/save.svg", theme));
  ui->buttonLibraryBox->setIcon(LoadSvg(":/resources/svg/apps_box.svg", theme));

  auto style = GetLuaSyntaxStyle(theme);

  ui->globalVarsText->setSyntaxStyle(style);
  ui->globalVarsTextBatch->setSyntaxStyle(style);

  ui->functionText->setSyntaxStyle(style);
  ui->functionTextBatch->setSyntaxStyle(style);
}

FunctionEditorWidget::FunctionEditorWidget(PlotDataMapRef& plotMapData,
                                           const TransformsMap& mapped_custom_plots,
                                           QWidget* parent)
  : QWidget(parent)
  , _plot_map_data(plotMapData)
  , _transform_maps(mapped_custom_plots)
  , ui(new Ui::FunctionEditor)
  , _functions_library_ui(nullptr)
  , _functions_library_dialog(nullptr)
  , _functions_library_overlay(nullptr)
  , _v_count(1)
  , _preview_widget(new PlotWidget(_local_plot_data, this))
{
  ui->setupUi(this);

  setupFunctionAppsButton();

  ui->globalVarsText->setHighlighter(new QLuaHighlighter);
  ui->globalVarsTextBatch->setHighlighter(new QLuaHighlighter);

  ui->functionText->setHighlighter(new QLuaHighlighter);
  ui->functionTextBatch->setHighlighter(new QLuaHighlighter);

  lua_completer_ = new QLuaCompleter(this);
  lua_completer_batch_ = new QLuaCompleter(this);

  ui->globalVarsText->setCompleter(lua_completer_);
  ui->globalVarsTextBatch->setCompleter(lua_completer_);

  ui->functionText->setCompleter(lua_completer_batch_);
  ui->functionTextBatch->setCompleter(lua_completer_batch_);

  QSettings settings;

  this->setWindowTitle("Create a custom timeseries");

  QFont fixedFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
  fixedFont.setPointSize(10);

  ui->globalVarsText->setFont(fixedFont);
  ui->functionText->setFont(fixedFont);
  ui->globalVarsTextBatch->setFont(fixedFont);
  ui->functionTextBatch->setFont(fixedFont);

  auto theme = settings.value("StyleSheet::theme", "light").toString();
  on_stylesheetChanged(theme);

  QPalette palette = ui->listAdditionalSources->palette();
  palette.setBrush(QPalette::Highlight, palette.brush(QPalette::Base));
  palette.setBrush(QPalette::HighlightedText, palette.brush(QPalette::Text));
  ui->listAdditionalSources->setPalette(palette);

  QStringList numericPlotNames;
  for (const auto& p : _plot_map_data.numeric)
  {
    QString name = QString::fromStdString(p.first);
    numericPlotNames.push_back(name);
  }
  numericPlotNames.sort(Qt::CaseInsensitive);

  QByteArray saved_xml =
      settings.value("FunctionEditorWidget.recentSnippetsXML", QByteArray()).toByteArray();
  restoreGeometry(settings.value("FunctionEditorWidget.geometry").toByteArray());

  if (saved_xml.isEmpty())
  {
    QFile file("://resources/default.snippets.xml");
    if (!file.open(QIODevice::ReadOnly))
    {
      throw std::runtime_error("problem with default.snippets.xml");
    }
    saved_xml = file.readAll();
  }

  importSnippets(saved_xml);

  ui->globalVarsText->setPlainText(
      settings.value("FunctionEditorWidget.previousGlobals", "").toString());
  ui->globalVarsTextBatch->setPlainText(
      settings.value("FunctionEditorWidget.previousGlobalsBatch", "").toString());

  ui->functionText->setPlainText(
      settings.value("FunctionEditorWidget.previousFunction", "return value").toString());
  ui->functionTextBatch->setPlainText(
      settings.value("FunctionEditorWidget.previousFunctionBatch", "return value").toString());

  ui->lineEditSource->installEventFilter(this);
  ui->listAdditionalSources->installEventFilter(this);
  ui->lineEditTab2Filter->installEventFilter(this);

  auto preview_layout = new QHBoxLayout(ui->framePlotPreview);
  preview_layout->setMargin(6);
  preview_layout->addWidget(_preview_widget);

  _preview_widget->setContextMenuEnabled(false);

  _update_preview_tab1.connectCallback([this]() { onUpdatePreview(); });
  onUpdatePreview();
  _update_preview_tab2.connectCallback([this]() { onUpdatePreviewBatch(); });
  onUpdatePreviewBatch();

  _tab2_filter.connectCallback([this]() { onLineEditTab2FilterChanged(); });

  int batch_filter_type = settings.value("FunctionEditorWidget.filterType", 2).toInt();
  switch (batch_filter_type)
  {
    case 1:
      ui->radioButtonContains->setChecked(true);
      break;
    case 2:
      ui->radioButtonWildcard->setChecked(true);
      break;
    case 3:
      ui->radioButtonRegExp->setChecked(true);
      break;
  }

  bool use_batch_prefix = settings.value("FunctionEditorWidget.batchPrefix", false).toBool();
  ui->radioButtonPrefix->setChecked(use_batch_prefix);
}

void FunctionEditorWidget::setupFunctionAppsButton()
{
  connect(ui->buttonLibraryBox, &QToolButton::clicked, this, [this]() {
    if (!_functions_library_dialog)
    {
      _functions_library_dialog = new QDialog(this);
      _functions_library_ui = new Ui::FunctionsLibrary();
      _functions_library_ui->setupUi(_functions_library_dialog);

      reloadFunctionsLibraryTable();
      updateFunctionsLibraryPreview();

      _functions_library_dialog->adjustSize();

      _functions_library_dialog->setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
      _functions_library_dialog->setAttribute(Qt::WA_TranslucentBackground, true);

      _functions_library_dialog->installEventFilter(this);

      connect(_functions_library_ui->tableFunctions, &QTableWidget::cellDoubleClicked, this,
              [this](int, int) {
                if (_functions_library_ui && _functions_library_ui->useButton)
                {
                  _functions_library_ui->useButton->click();
                }
              });

      connect(_functions_library_dialog, &QObject::destroyed, this, [this]() {
        if (_functions_library_overlay)
        {
          _functions_library_overlay->hide();
          _functions_library_overlay->deleteLater();
          _functions_library_overlay = nullptr;
        }
        _functions_library_dialog = nullptr;
        delete _functions_library_ui;
        _functions_library_ui = nullptr;
      });

      connect(_functions_library_ui->tableFunctions, &QTableWidget::currentCellChanged, this,
              [this](int currentRow, int, int, int) {
                if (currentRow < 0)
                {
                  return;
                }
                auto item = _functions_library_ui->tableFunctions->item(currentRow, 0);
                if (!item)
                {
                  return;
                }
                _selected_library_name = item->text();
                updateFunctionsLibraryPreview();
              });

      connect(_functions_library_ui->useButton, &QPushButton::clicked, this, [this]() {
        if (_selected_library_name.isEmpty())
        {
          return;
        }

        auto it = _snipped_saved.find(_selected_library_name);
        if (it == _snipped_saved.end())
        {
          return;
        }

        const auto& sn = it->second;
        ui->globalVarsText->setPlainText(sn.global_vars);
        ui->functionText->setPlainText(sn.function);
        onUpdatePreview();

        _functions_library_dialog->hide();
      });
    }

    QWidget* top = window();

    if (!_functions_library_overlay)
    {
      _functions_library_overlay = new QWidget(top);
      _functions_library_overlay->setObjectName("functionsLibraryOverlay");
      _functions_library_overlay->setStyleSheet(
          "#functionsLibraryOverlay { background-color: rgba(0,0,0,90); }");
      _functions_library_overlay->setFocusPolicy(Qt::StrongFocus);
      _functions_library_overlay->show();
    }

    _functions_library_overlay->setGeometry(top->rect());
    _functions_library_overlay->raise();
    _functions_library_overlay->show();

    QPoint anchor = ui->globalVarsText->mapToGlobal(
        QPoint(ui->globalVarsText->width(), ui->globalVarsText->height()));

    int x = anchor.x() - _functions_library_dialog->width();
    int y = anchor.y() - _functions_library_dialog->height();

    _functions_library_dialog->move(x, y);
    _functions_library_dialog->show();
    _functions_library_dialog->raise();
    _functions_library_dialog->activateWindow();

    if (_functions_library_ui && _functions_library_ui->searchLineEdit)
    {
      _functions_library_ui->searchLineEdit->setFocus();
      _functions_library_ui->searchLineEdit->selectAll();
    }
  });
}

////// NEW //////////////// ////// NEW //////////////// ////// NEW ////////////////

void FunctionEditorWidget::reloadFunctionsLibraryTable()
{
  if (!_functions_library_ui || !_functions_library_ui->tableFunctions)
  {
    return;
  }

  auto t = _functions_library_ui->tableFunctions;

  _selected_library_name.clear();

  t->clear();
  t->setColumnCount(1);
  t->setHorizontalHeaderLabels({ "Name" });
  t->setRowCount((int)_snipped_saved.size());

  t->setEditTriggers(QAbstractItemView::NoEditTriggers);
  t->setSelectionBehavior(QAbstractItemView::SelectRows);
  t->setSelectionMode(QAbstractItemView::SingleSelection);

  t->horizontalHeader()->setStretchLastSection(true);
  t->verticalHeader()->setVisible(false);

  int row = 0;
  for (const auto& it : _snipped_saved)
  {
    t->setItem(row, 0, new QTableWidgetItem(it.first));
    row++;
  }

  if (t->rowCount() > 0)
  {
    t->selectRow(0);
    auto item0 = t->item(0, 0);
    if (item0)
    {
      _selected_library_name = item0->text();
    }
  }
}

void FunctionEditorWidget::updateFunctionsLibraryPreview()
{
  if (!_functions_library_ui)
  {
    return;
  }

  auto preview = _functions_library_ui->previewPlainText;
  if (!preview)
  {
    return;
  }

  auto it = _snipped_saved.find(_selected_library_name);
  if (it == _snipped_saved.end())
  {
    preview->clear();
    return;
  }

  const SnippetData& snippet = it->second;

  QString text;
  if (!snippet.global_vars.isEmpty())
  {
    text += snippet.global_vars + "\n\n";
  }

  text += "function calc(time, value";
  for (int i = 1; i <= snippet.additional_sources.size(); i++)
  {
    text += QString(", v%1").arg(i);
  }
  text += ")\n";

  const auto lines = snippet.function.split("\n");
  for (const auto& line : lines)
  {
    text += "    " + line + "\n";
  }
  text += "end";

  preview->setPlainText(text);
}

////// ////////////////////// //////////////////////  ////////////////

void FunctionEditorWidget::saveSettings()
{
  QSettings settings;
  settings.setValue("FunctionEditorWidget.recentSnippetsXML", exportSnippets());
  settings.setValue("FunctionEditorWidget.geometry", saveGeometry());

  settings.setValue("FunctionEditorWidget.previousGlobals", ui->globalVarsText->toPlainText());
  settings.setValue("FunctionEditorWidget.previousGlobalsBatch",
                    ui->globalVarsTextBatch->toPlainText());

  settings.setValue("FunctionEditorWidget.previousFunction", ui->functionText->toPlainText());
  settings.setValue("FunctionEditorWidget.previousFunctionBatch",
                    ui->functionTextBatch->toPlainText());
  int batch_filter_type = 0;
  if (ui->radioButtonContains->isChecked())
  {
    batch_filter_type = 1;
  }
  else if (ui->radioButtonWildcard->isChecked())
  {
    batch_filter_type = 2;
  }
  if (ui->radioButtonRegExp->isChecked())
  {
    batch_filter_type = 3;
  }
  settings.setValue("FunctionEditorWidget.filterType", batch_filter_type);

  settings.setValue("FunctionEditorWidget.batchPrefix", ui->radioButtonPrefix->isChecked());
}

FunctionEditorWidget::~FunctionEditorWidget()
{
  delete _preview_widget;

  saveSettings();

  delete ui;
}

void FunctionEditorWidget::setLinkedPlotName(const QString& linkedPlotName)
{
  ui->lineEditSource->setText(linkedPlotName);
}

void FunctionEditorWidget::clear()
{
  ui->lineEditSource->setText("");
  ui->nameLineEdit->setText("");
  ui->listAdditionalSources->setRowCount(0);

  ui->suffixLineEdit->setText("");
  ui->listBatchSources->clear();
  ui->lineEditTab2Filter->setText("");
}

QString FunctionEditorWidget::getLinkedData() const
{
  return ui->lineEditSource->text();
}

void FunctionEditorWidget::createNewPlot()
{
  ui->nameLineEdit->setEnabled(true);
  ui->lineEditSource->setEnabled(true);
  _editor_mode = CREATE;
}

void FunctionEditorWidget::editExistingPlot(CustomPlotPtr data)
{
  ui->globalVarsText->setPlainText(data->snippet().global_vars);
  ui->functionText->setPlainText(data->snippet().function);
  setLinkedPlotName(data->snippet().linked_source);
  ui->nameLineEdit->setText(data->aliasName());
  ui->nameLineEdit->setEnabled(false);

  _editor_mode = MODIFY;

  auto list_widget = ui->listAdditionalSources;
  list_widget->setRowCount(0);

  for (QString curve_name : data->snippet().additional_sources)
  {
    if (list_widget->findItems(curve_name, Qt::MatchExactly).isEmpty() &&
        curve_name != ui->lineEditSource->text())
    {
      int row = list_widget->rowCount();
      list_widget->setRowCount(row + 1);
      list_widget->setItem(row, 0, new QTableWidgetItem(QString("v%1").arg(row + 1)));
      list_widget->setItem(row, 1, new QTableWidgetItem(curve_name));
    }
  }
  on_listSourcesChanged();
}

// CustomPlotPtr FunctionEditorWidget::getCustomPlotData() const
//{
//  return _plot;
//}

bool FunctionEditorWidget::eventFilter(QObject* obj, QEvent* ev)
{
  if (obj == _functions_library_dialog && ev->type() == QEvent::Hide)
  {
    if (_functions_library_overlay)
    {
      _functions_library_overlay->hide();
    }
    return false;
  }

  if (ev->type() == QEvent::DragEnter)
  {
    auto event = static_cast<QDragEnterEvent*>(ev);
    const QMimeData* mimeData = event->mimeData();
    QStringList mimeFormats = mimeData->formats();

    for (const QString& format : mimeFormats)
    {
      QByteArray encoded = mimeData->data(format);
      QDataStream stream(&encoded, QIODevice::ReadOnly);

      if (format != "curveslist/add_curve")
      {
        return false;
      }

      _dragging_curves.clear();

      while (!stream.atEnd())
      {
        QString curve_name;
        stream >> curve_name;
        if (!curve_name.isEmpty())
        {
          _dragging_curves.push_back(curve_name);
        }
      }
      if ((obj == ui->lineEditSource && _dragging_curves.size() == 1) ||
          (obj == ui->lineEditTab2Filter && _dragging_curves.size() == 1) ||
          (obj == ui->listAdditionalSources && _dragging_curves.size() > 0))
      {
        event->acceptProposedAction();
        return true;
      }
    }
  }
  else if (ev->type() == QEvent::Drop)
  {
    if (obj == ui->lineEditSource)
    {
      ui->lineEditSource->setText(_dragging_curves.front());
    }
    else if (obj == ui->lineEditTab2Filter)
    {
      ui->lineEditTab2Filter->setText(_dragging_curves.front());
    }
    else if (obj == ui->listAdditionalSources)
    {
      auto list_widget = ui->listAdditionalSources;
      for (QString curve_name : _dragging_curves)
      {
        if (list_widget->findItems(curve_name, Qt::MatchExactly).isEmpty() &&
            curve_name != ui->lineEditSource->text())
        {
          int row = list_widget->rowCount();
          list_widget->setRowCount(row + 1);
          list_widget->setItem(row, 0, new QTableWidgetItem(QString("v%1").arg(row + 1)));
          list_widget->setItem(row, 1, new QTableWidgetItem(curve_name));
        }
      }
      on_listSourcesChanged();
    }
  }

  return false;
}

void FunctionEditorWidget::importSnippets(const QByteArray& xml_text)
{
  _snipped_saved = GetSnippetsFromXML(xml_text);

  for (const auto& custom_it : _transform_maps)
  {
    auto math_plot = dynamic_cast<LuaCustomFunction*>(custom_it.second.get());
    if (!math_plot)
    {
      continue;
    }

    SnippetData snippet;
    snippet.alias_name = math_plot->aliasName();

    if (_snipped_saved.count(snippet.alias_name) > 0)
    {
      continue;
    }

    snippet.global_vars = math_plot->snippet().global_vars;
    snippet.function = math_plot->snippet().function;
    _snipped_saved.insert({ snippet.alias_name, snippet });
  }

  if (_functions_library_ui)
  {
    reloadFunctionsLibraryTable();
    updateFunctionsLibraryPreview();
  }
}

QByteArray FunctionEditorWidget::exportSnippets() const
{
  QDomDocument doc;
  auto root = ExportSnippets(_snipped_saved, doc);
  doc.appendChild(root);
  return doc.toByteArray(2);
}

void FunctionEditorWidget::on_nameLineEdit_textChanged(const QString& name)
{
  if (_plot_map_data.numeric.count(name.toStdString()) == 0)
  {
    ui->pushButtonCreate->setText("Create New Timeseries");
  }
  else
  {
    ui->pushButtonCreate->setText("Modify Timeseries");
  }
  updatePreview();
}

void FunctionEditorWidget::on_buttonLoadFunctions_clicked()
{
  QSettings settings;
  QString directory_path =
      settings.value("AddCustomPlotDialog.loadDirectory", QDir::currentPath()).toString();

  QString fileName = QFileDialog::getOpenFileName(this, tr("Open Snippet Library"), directory_path,
                                                  tr("Snippets (*.snippets.xml)"));
  if (fileName.isEmpty())
  {
    return;
  }

  QFile file(fileName);

  if (!file.open(QIODevice::ReadOnly))
  {
    QMessageBox::critical(this, "Error", QString("Failed to open the file [%1]").arg(fileName));
    return;
  }

  directory_path = QFileInfo(fileName).absolutePath();
  settings.setValue("AddCustomPlotDialog.loadDirectory", directory_path);

  importSnippets(file.readAll());
}

void FunctionEditorWidget::on_buttonSaveFunctions_clicked()
{
  QSettings settings;
  QString directory_path =
      settings.value("AddCustomPlotDialog.loadDirectory", QDir::currentPath()).toString();

  QString fileName = QFileDialog::getSaveFileName(this, tr("Open Snippet Library"), directory_path,
                                                  tr("Snippets (*.snippets.xml)"));

  if (fileName.isEmpty())
  {
    return;
  }
  if (!fileName.endsWith(".snippets.xml"))
  {
    fileName.append(".snippets.xml");
  }

  QFile file(fileName);
  if (!file.open(QIODevice::WriteOnly))
  {
    QMessageBox::critical(this, "Error", QString("Failed to open the file [%1]").arg(fileName));
    return;
  }
  auto data = exportSnippets();

  file.write(data);
  file.close();

  directory_path = QFileInfo(fileName).absolutePath();
  settings.setValue("AddCustomPlotDialog.loadDirectory", directory_path);
}

void FunctionEditorWidget::on_buttonSaveCurrent_clicked()
{
  QString name = _selected_library_name;

  if (_functions_library_ui && _functions_library_ui->tableFunctions)
  {
    int r = _functions_library_ui->tableFunctions->currentRow();
    if (r >= 0)
    {
      auto item = _functions_library_ui->tableFunctions->item(r, 0);
      if (item)
      {
        name = item->text();
      }
    }
  }

  bool ok = false;
  name = QInputDialog::getText(this, tr("Name of the Function"), tr("Name:"), QLineEdit::Normal,
                               name, &ok);
  if (!ok || name.isEmpty())
  {
    return;
  }

  SnippetData snippet;
  snippet.alias_name = name;
  snippet.global_vars = ui->globalVarsText->toPlainText();
  snippet.function = ui->functionText->toPlainText();

  addToSaved(name, snippet);
}

bool FunctionEditorWidget::addToSaved(const QString& name, const SnippetData& snippet)
{
  if (_snipped_saved.count(name))
  {
    QMessageBox msgBox(this);
    msgBox.setWindowTitle("Warning");
    msgBox.setText(
        tr("A function with the same name exists already in the list of saved functions.\n"));
    msgBox.addButton(QMessageBox::Cancel);
    QPushButton* button = msgBox.addButton(tr("Overwrite"), QMessageBox::YesRole);
    msgBox.setDefaultButton(button);

    int res = msgBox.exec();
    if (res < 0 || res == QMessageBox::Cancel)
    {
      return false;
    }
  }

  _snipped_saved[name] = snippet;

  if (_functions_library_ui)
  {
    reloadFunctionsLibraryTable();
    _selected_library_name = name;
    updateFunctionsLibraryPreview();
  }

  return true;
}

void FunctionEditorWidget::on_pushButtonCreate_clicked()
{
  std::vector<CustomPlotPtr> created_plots;

  try
  {
    if (ui->tabWidget->currentIndex() == 0)
    {
      std::string new_plot_name = ui->nameLineEdit->text().toStdString();

      if (_editor_mode == CREATE && _transform_maps.count(new_plot_name) != 0)
      {
        QMessageBox msgBox(this);
        msgBox.setWindowTitle("Warning");
        msgBox.setText(tr("A custom time series with the same name exists already.\n"
                          " Do you want to overwrite it?\n"));
        msgBox.addButton(QMessageBox::Cancel);
        QPushButton* button = msgBox.addButton(tr("Overwrite"), QMessageBox::YesRole);
        msgBox.setDefaultButton(button);

        int res = msgBox.exec();

        if (res < 0 || res == QMessageBox::Cancel)
        {
          return;
        }
      }

      SnippetData snippet;
      snippet.function = ui->functionText->toPlainText();
      snippet.global_vars = ui->globalVarsText->toPlainText();
      snippet.alias_name = ui->nameLineEdit->text();
      snippet.linked_source = getLinkedData();
      for (int row = 0; row < ui->listAdditionalSources->rowCount(); row++)
      {
        snippet.additional_sources.push_back(ui->listAdditionalSources->item(row, 1)->text());
      }
      created_plots.push_back(std::make_unique<LuaCustomFunction>(snippet));
    }
    else  // ----------- batch ------
    {
      for (int row = 0; row < ui->listBatchSources->count(); row++)
      {
        SnippetData snippet;
        snippet.function = ui->functionTextBatch->toPlainText();
        snippet.global_vars = ui->globalVarsTextBatch->toPlainText();
        snippet.linked_source = ui->listBatchSources->item(row)->text();
        if (ui->radioButtonPrefix->isChecked())
        {
          snippet.alias_name = ui->suffixLineEdit->text() + snippet.linked_source;
        }
        else
        {
          snippet.alias_name = snippet.linked_source + ui->suffixLineEdit->text();
        }
        created_plots.push_back(std::make_unique<LuaCustomFunction>(snippet));
      }
    }

    accept(created_plots);
    saveSettings();
  }
  catch (const std::runtime_error& e)
  {
    QMessageBox::critical(this, "Error",
                          "Failed to create math plot : " + QString::fromStdString(e.what()));
  }
}

void FunctionEditorWidget::on_pushButtonCancel_pressed()
{
  if (_editor_mode == MODIFY)
  {
    clear();
  }
  saveSettings();
  closed();
}

void FunctionEditorWidget::on_listSourcesChanged()
{
  QString function_text("function( time, value");
  for (int row = 0; row < ui->listAdditionalSources->rowCount(); row++)
  {
    function_text += ", ";
    function_text += ui->listAdditionalSources->item(row, 0)->text();
  }
  function_text += " )";
  ui->labelFunction->setText(function_text);

  updatePreview();
}

void FunctionEditorWidget::on_listAdditionalSources_itemSelectionChanged()
{
  bool any_selected = !ui->listAdditionalSources->selectedItems().isEmpty();
  ui->pushButtonDeleteCurves->setEnabled(any_selected);
}

void FunctionEditorWidget::on_pushButtonDeleteCurves_clicked()
{
  auto list_sources = ui->listAdditionalSources;
  QModelIndexList selected = list_sources->selectionModel()->selectedRows();
  while (selected.size() > 0)
  {
    list_sources->removeRow(selected.first().row());
    selected = list_sources->selectionModel()->selectedRows();
  }
  for (int row = 0; row < list_sources->rowCount(); row++)
  {
    list_sources->item(row, 0)->setText(QString("v%1").arg(row + 1));
  }

  on_listAdditionalSources_itemSelectionChanged();
  on_listSourcesChanged();
}

void FunctionEditorWidget::on_lineEditSource_textChanged(const QString& text)
{
  updatePreview();
}

void FunctionEditorWidget::updatePreview()
{
  _update_preview_tab1.triggerSignal(250);
}

void FunctionEditorWidget::setSemaphore(QLabel* semaphore, QString errors)
{
  QFile file(":/resources/svg/red_circle.svg");

  if (errors.isEmpty())
  {
    errors = "Everything is fine :)";
    file.setFileName(":/resources/svg/green_circle.svg");
    ui->pushButtonCreate->setEnabled(true);
  }
  else
  {
    errors = errors.left(errors.size() - 1);
    ui->pushButtonCreate->setEnabled(false);
  }

  semaphore->setToolTip(errors);
  semaphore->setToolTipDuration(5000);

  file.open(QFile::ReadOnly | QFile::Text);
  auto svg_data = file.readAll();
  file.close();
  QByteArray content(svg_data);
  QSvgRenderer rr(content);
  QImage image(26, 26, QImage::Format_ARGB32);
  QPainter painter(&image);
  image.fill(Qt::transparent);
  rr.render(&painter);
  semaphore->setPixmap(QPixmap::fromImage(image));
}

void FunctionEditorWidget::onUpdatePreview()
{
  QString errors;
  std::string new_plot_name = ui->nameLineEdit->text().toStdString();

  if (_transform_maps.count(new_plot_name) != 0)
  {
    QString new_name = ui->nameLineEdit->text();
    if (ui->lineEditSource->text().toStdString() == new_plot_name ||
        !ui->listAdditionalSources->findItems(new_name, Qt::MatchExactly).isEmpty())
    {
      errors += "- The name of the new timeseries is the same of one of its "
                "dependencies.\n";
    }
  }

  if (new_plot_name.empty())
  {
    errors += "- Missing name of the new time series.\n";
  }
  else
  {
    // check if name is unique (except if is custom_plot)
    if (_plot_map_data.numeric.count(new_plot_name) != 0 &&
        _transform_maps.count(new_plot_name) == 0)
    {
      errors += "- Plot name already exists and can't be modified.\n";
    }
  }

  if (ui->lineEditSource->text().isEmpty())
  {
    errors += "- Missing source time series.\n";
  }

  SnippetData snippet;
  snippet.function = ui->functionText->toPlainText();
  snippet.global_vars = ui->globalVarsText->toPlainText();
  snippet.alias_name = ui->nameLineEdit->text();
  snippet.linked_source = getLinkedData();
  for (int row = 0; row < ui->listAdditionalSources->rowCount(); row++)
  {
    snippet.additional_sources.push_back(ui->listAdditionalSources->item(row, 1)->text());
  }

  CustomPlotPtr lua_function;
  try
  {
    lua_function = std::make_unique<LuaCustomFunction>(snippet);
    ui->buttonSaveCurrent->setEnabled(true);
  }
  catch (std::runtime_error& err)
  {
    errors += QString("- Error in Lua script: %1").arg(err.what());
    ui->buttonSaveCurrent->setEnabled(false);
  }

  if (lua_function)
  {
    try
    {
      std::string name = new_plot_name.empty() ? "no_name" : new_plot_name;
      PlotData& out_data = _local_plot_data.getOrCreateNumeric(name);
      out_data.clear();

      std::vector<PlotData*> out_vector = { &out_data };
      lua_function->setData(&_plot_map_data, {}, out_vector);
      lua_function->calculate();

      _preview_widget->removeAllCurves();
      _preview_widget->addCurve(name, Qt::blue);
      _preview_widget->zoomOut(false);
    }
    catch (std::runtime_error& err)
    {
      errors += QString("- Error in Lua script: %1").arg(err.what());
    }
  }

  setSemaphore(ui->labelSemaphore, errors);
}

void FunctionEditorWidget::onUpdatePreviewBatch()
{
  QString errors;

  if (ui->suffixLineEdit->text().isEmpty())
  {
    errors += "- Missing prefix/suffix.\n";
  }

  if (ui->listBatchSources->count() == 0)
  {
    errors += "- No input series.\n";
  }

  SnippetData snippet;
  snippet.function = ui->functionTextBatch->toPlainText();
  snippet.global_vars = ui->globalVarsTextBatch->toPlainText();

  try
  {
    auto lua_function = std::make_unique<LuaCustomFunction>(snippet);
  }
  catch (std::runtime_error& err)
  {
    errors += QString("- Error in Lua script: %1").arg(err.what());
  }

  setSemaphore(ui->labelSemaphoreBatch, errors);
}

void FunctionEditorWidget::on_pushButtonHelp_clicked()
{
  QDialog* dialog = new QDialog(this);
  auto ui = new Ui_FunctionEditorHelp();
  ui->setupUi(dialog);

  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->exec();
}

void FunctionEditorWidget::onLineEditTab2FilterChanged()
{
  QString filter_text = ui->lineEditTab2Filter->text();
  ui->listBatchSources->clear();

  if (ui->radioButtonRegExp->isChecked() || ui->radioButtonWildcard->isChecked())
  {
    QRegExp rx(filter_text);
    if (ui->radioButtonWildcard->isChecked())
    {
      rx.setPatternSyntax(QRegExp::Wildcard);
    }

    for (const auto& [name, plotdata] : _plot_map_data.numeric)
    {
      auto qname = QString::fromStdString(name);
      if (rx.exactMatch(qname))
      {
        ui->listBatchSources->addItem(qname);
      }
    }
  }
  else
  {
    QStringList spaced_items = filter_text.split(' ', PJ::SkipEmptyParts);
    for (const auto& [name, plotdata] : _plot_map_data.numeric)
    {
      bool show = true;
      auto qname = QString::fromStdString(name);
      for (const auto& part : spaced_items)
      {
        if (qname.contains(part) == false)
        {
          show = false;
          break;
        }
      }
      if (show)
      {
        ui->listBatchSources->addItem(qname);
      }
    }
  }
  ui->listBatchSources->sortItems();
  onUpdatePreviewBatch();
}

void FunctionEditorWidget::on_pushButtonHelpTab2_clicked()
{
  on_pushButtonHelp_clicked();
}

void FunctionEditorWidget::on_lineEditTab2Filter_textChanged(const QString& arg1)
{
  _tab2_filter.triggerSignal(250);
}

void FunctionEditorWidget::on_functionTextBatch_textChanged()
{
  _update_preview_tab2.triggerSignal(250);
}

void FunctionEditorWidget::on_suffixLineEdit_textChanged(const QString& arg1)
{
  _update_preview_tab2.triggerSignal(250);
}

void FunctionEditorWidget::on_tabWidget_currentChanged(int index)
{
  if (index == 0)
  {
    onUpdatePreview();
  }
  else
  {
    onUpdatePreviewBatch();
  }
}

void FunctionEditorWidget::on_globalVarsTextBatch_textChanged()
{
  _update_preview_tab2.triggerSignal(250);
}

void FunctionEditorWidget::on_globalVarsText_textChanged()
{
  _update_preview_tab1.triggerSignal(250);
}

void FunctionEditorWidget::on_functionText_textChanged()
{
  _update_preview_tab1.triggerSignal(250);
}
