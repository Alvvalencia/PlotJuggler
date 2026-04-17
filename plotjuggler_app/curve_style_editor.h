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
#include "color_picker_popup.h"

namespace Ui
{
class CurveStyleEditor;
}

class EditorRowWidget;

// Small clickable colored square; emits clicked() on left mouse press.
class ColorSwatch : public QWidget
{
  Q_OBJECT
public:
  explicit ColorSwatch(QColor color, QWidget* parent = nullptr);

  void setColor(QColor c);
  QColor color() const
  {
    return _color;
  }

signals:
  void clicked();

protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;

private:
  QColor _color;
};

class EditorRowWidget : public QWidget
{
  Q_OBJECT

public:
  EditorRowWidget(QString text, QColor color);

  QString text() const;

  void setColor(QColor color);
  QColor color() const;

signals:
  void colorClicked(EditorRowWidget* self);

private:
  QLabel* _text;
  QColor _color;
  ColorSwatch* _swatch;
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
  void on_listWidget_itemSelectionChanged();
  void on_buttonDeleteCurve_clicked();
  void on_checkBoxMax_toggled(bool checked);
  void on_checkBoxMin_toggled(bool checked);
  void on_pushButtonReset_clicked();
  void on_lineLimitMin_textChanged(const QString& text);
  void on_lineLimitMax_textChanged(const QString& text);
  void onComboWidthChanged(int index);

private:
  Ui::CurveStyleEditor* ui;
  ColorPickerPopup* _color_picker_popup = nullptr;  // lazy-created on first open
  std::vector<PlotWidget*> _target_plots;

  // Active swatch click target — set when a row's swatch is clicked, used by
  // the popup's persistent colorChanged slot to know which curve to update.
  // Cleared when the row goes away (deletion / refresh / plot destruction).
  EditorRowWidget* _active_color_row = nullptr;
  PlotWidget* _active_color_plot = nullptr;
  QString _active_color_curve;

  static constexpr int ROLE_CURVE_NAME = Qt::UserRole;
  static constexpr int ROLE_PLOT_PTR = Qt::UserRole + 1;

  void setupTable();
  void updateLimits();
  void onColorClicked(EditorRowWidget* row);
  void onPickerColorChanged(QColor c);
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
