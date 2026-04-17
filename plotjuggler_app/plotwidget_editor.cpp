/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "plotwidget_editor.h"
#include "ui_plotwidget_editor.h"

#include <QVBoxLayout>
#include <QSettings>

PlotwidgetEditor::PlotwidgetEditor(PlotWidget* plotwidget, QWidget* parent)
  : QDialog(parent), ui(new Ui::PlotWidgetEditor), _plotwidget_origin(plotwidget)
{
  ui->setupUi(this);

  QDomDocument doc;
  auto saved_state = plotwidget->xmlSaveState(doc);

  _plotwidget = new PlotWidget(plotwidget->datamap(), this);
  _plotwidget->xmlLoadState(saved_state);
  _plotwidget->on_changeTimeOffset(plotwidget->timeOffset());
  _plotwidget->setContextMenuEnabled(false);

  _bounding_rect_original = _plotwidget_origin->currentBoundingRect();

  auto preview_layout = new QVBoxLayout();
  ui->framePlotPreview->setLayout(preview_layout);
  preview_layout->addWidget(_plotwidget);
  preview_layout->setMargin(6);

  _plotwidget->zoomOut(false);

  _curve_editor = new CurveStyleEditor(this);
  ui->editorLayout->addWidget(_curve_editor);
  _curve_editor->setTargetPlots({ _plotwidget });

  QSettings settings;
  restoreGeometry(settings.value("PlotwidgetEditor.geometry").toByteArray());
}

PlotwidgetEditor::~PlotwidgetEditor()
{
  QSettings settings;
  settings.setValue("PlotwidgetEditor.geometry", saveGeometry());

  delete _plotwidget;
  delete ui;
}

void PlotwidgetEditor::on_pushButtonCancel_pressed()
{
  this->reject();
}

void PlotwidgetEditor::on_pushButtonSave_pressed()
{
  QDomDocument doc;
  _plotwidget->setZoomRectangle(_bounding_rect_original, false);
  auto elem = _plotwidget->xmlSaveState(doc);
  _plotwidget_origin->xmlLoadState(elem);
  this->accept();
}
