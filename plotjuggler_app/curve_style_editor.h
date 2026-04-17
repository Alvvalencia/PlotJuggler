/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef CURVE_STYLE_EDITOR_H
#define CURVE_STYLE_EDITOR_H

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QListWidgetItem>
#include <optional>
#include <vector>

#include "plotwidget.h"
#include "color_wheel.hpp"
#include "color_preview.hpp"

namespace Ui
{
class CurveStyleEditor;
}

class EditorRowWidget : public QWidget
{
  Q_OBJECT

public:
  EditorRowWidget(QString text, QColor color);

  void enterEvent(QEvent* ev) override;
  void leaveEvent(QEvent* ev) override;

  QString text() const;

  void setColor(QColor color);
  QColor color() const;

signals:
  void deleteRow(QWidget* _this);

private:
  QLabel* _text;
  QColor _color;
  QPushButton* _delete_button;
  QWidget* _empty_spacer;
};

class CurveStyleEditor : public QWidget
{
  Q_OBJECT

public:
  explicit CurveStyleEditor(QWidget* parent = nullptr);
  ~CurveStyleEditor();

  void setTargetPlots(const std::vector<PlotWidget*>& plots);
  const std::vector<PlotWidget*>& targetPlots() const;

public slots:
  void refreshFromPlots();

private slots:
  void onColorChanged(QColor c);
  void on_editColorText_textChanged(const QString& text);
  void on_listWidget_itemSelectionChanged();
  void on_checkBoxMax_toggled(bool checked);
  void on_checkBoxMin_toggled(bool checked);
  void on_pushButtonReset_clicked();
  void on_lineLimitMin_textChanged(const QString& text);
  void on_lineLimitMax_textChanged(const QString& text);
  void onComboWidthChanged(int index);

private:
  Ui::CurveStyleEditor* ui;
  color_widgets::ColorWheel* _color_wheel;
  color_widgets::ColorPreview* _color_preview;
  std::vector<PlotWidget*> _target_plots;

  static constexpr int ROLE_CURVE_NAME = Qt::UserRole;
  static constexpr int ROLE_PLOT_PTR = Qt::UserRole + 1;

  void setupColorWidget();
  void setupTable();
  void updateLimits();
  void onDeleteRow(QWidget* w);
  void disableWidgets();
  void enableWidgets();
  void applyStyleToPlots(std::optional<PlotWidgetBase::CurveStyle> style);
  void disconnectFromPlots();
  void connectToPlots();

  // Reads style/width/limits from activePlots().front() and pushes them into
  // the radio buttons, width combo, and limit fields. Called when the plot set
  // changes (refreshFromPlots) and when the user selects a different curve row
  // — so the controls always reflect the plot the next change will apply to.
  void syncControlsToActivePlot();

  // Per-plot operations (style, width, limits) target only the plot that owns
  // the currently-selected curve row; with no row selected they fall back to
  // every plot the editor is pointed at. Per-curve ops (color, delete) always
  // resolve their target via the row's ROLE_PLOT_PTR directly.
  std::vector<PlotWidget*> activePlots() const;

  std::vector<QMetaObject::Connection> _connections;
  QListWidgetItem* _last_clicked_item = nullptr;
};

#endif  // CURVE_STYLE_EDITOR_H
