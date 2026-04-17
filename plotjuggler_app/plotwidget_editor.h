/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef PLOTWIDGET_EDITOR_H
#define PLOTWIDGET_EDITOR_H

#include <QDialog>

#include "plotwidget.h"
#include "curve_style_editor.h"
#include "PlotJuggler/transform_function.h"

namespace Ui
{
class PlotWidgetEditor;
}

class PlotwidgetEditor : public QDialog
{
  Q_OBJECT

public:
  explicit PlotwidgetEditor(PlotWidget* plotwidget, QWidget* parent = nullptr);
  ~PlotwidgetEditor();

private slots:
  void on_pushButtonCancel_pressed();
  void on_pushButtonSave_pressed();

private:
  Ui::PlotWidgetEditor* ui;

  PlotWidget* _plotwidget;
  PlotWidget* _plotwidget_origin;
  QRectF _bounding_rect_original;
  CurveStyleEditor* _curve_editor;
};

#endif  // PLOTWIDGET_EDITOR_H
