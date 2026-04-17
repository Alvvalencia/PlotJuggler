/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef PLOTSIDE_EDITOR_H
#define PLOTSIDE_EDITOR_H

#include <QWidget>
#include <vector>

#include "plotwidget.h"
#include "curve_style_editor.h"

namespace Ui
{
class PlotsideEditor;
}

class PlotsideEditor : public QWidget
{
  Q_OBJECT

public:
  explicit PlotsideEditor(QWidget* parent = nullptr);
  ~PlotsideEditor();

  void setTargetPlots(const std::vector<PlotWidget*>& plots);
  const std::vector<PlotWidget*>& targetPlots() const;

private:
  Ui::PlotsideEditor* ui;
  CurveStyleEditor* _curve_editor;
};

#endif  // PLOTSIDE_EDITOR_H
