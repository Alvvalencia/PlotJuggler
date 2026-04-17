/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "plotside_editor.h"
#include "ui_plotside_editor.h"

PlotsideEditor::PlotsideEditor(QWidget* parent) : QWidget(parent), ui(new Ui::PlotsideEditor)
{
  ui->setupUi(this);

  _curve_editor = new CurveStyleEditor(this);
  ui->editorLayout->addWidget(_curve_editor);
}

PlotsideEditor::~PlotsideEditor()
{
  delete ui;
}

void PlotsideEditor::setTargetPlots(const std::vector<PlotWidget*>& plots)
{
  _curve_editor->setTargetPlots(plots);

  if (plots.empty())
  {
    ui->labelPlotName->setText("No plots");
  }
  else if (plots.size() == 1)
  {
    ui->labelPlotName->setText("1 plot selected");
  }
  else
  {
    ui->labelPlotName->setText(QString("%1 plots").arg(plots.size()));
  }
}

const std::vector<PlotWidget*>& PlotsideEditor::targetPlots() const
{
  return _curve_editor->targetPlots();
}
