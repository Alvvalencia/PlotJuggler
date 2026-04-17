/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "curve_style_editor.h"
#include "ui_curve_style_editor.h"

#include <QHBoxLayout>
#include <QListWidgetItem>
#include <QDoubleValidator>
#include <QMouseEvent>
#include <QPainter>
#include <algorithm>
#include <limits>

const double MAX_DOUBLE = std::numeric_limits<double>::max() / 2;

// ---------------------------------------------------------------------------
// ColorSwatch
// ---------------------------------------------------------------------------

ColorSwatch::ColorSwatch(QColor color, QWidget* parent) : QWidget(parent), _color(color)
{
  setFixedSize(18, 18);
  setCursor(Qt::PointingHandCursor);
}

void ColorSwatch::setColor(QColor c)
{
  if (_color == c)
  {
    return;
  }
  _color = c;
  update();
}

void ColorSwatch::paintEvent(QPaintEvent*)
{
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  p.setBrush(_color);
  p.setPen(QPen(Qt::black, 1));
  // Slight inset so the 1-px border doesn't get clipped by the widget edge.
  p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
}

void ColorSwatch::mousePressEvent(QMouseEvent* event)
{
  if (event->button() == Qt::LeftButton)
  {
    emit clicked();
  }
}

// ---------------------------------------------------------------------------
// EditorRowWidget
// ---------------------------------------------------------------------------

EditorRowWidget::EditorRowWidget(QString text, QColor color) : QWidget()
{
  _swatch = new ColorSwatch(color, this);
  _text = new QLabel(text, this);
  _color = color;
  setStyleSheet(QString("color: %1;").arg(color.name()));

  auto layout = new QHBoxLayout();
  setLayout(layout);
  layout->setContentsMargins(2, 0, 2, 0);
  layout->setSpacing(6);
  layout->addWidget(_swatch);
  layout->addWidget(_text);
  layout->addStretch();

  connect(_swatch, &ColorSwatch::clicked, this, [this]() { emit colorClicked(this); });
}

QString EditorRowWidget::text() const
{
  return _text->text();
}

void EditorRowWidget::setColor(QColor color)
{
  _color = color;
  setStyleSheet(QString("color: %1;").arg(color.name()));
  _swatch->setColor(color);
}

QColor EditorRowWidget::color() const
{
  return _color;
}

// ---------------------------------------------------------------------------
// CurveStyleEditor
// ---------------------------------------------------------------------------

CurveStyleEditor::CurveStyleEditor(QWidget* parent) : QWidget(parent), ui(new Ui::CurveStyleEditor)
{
  ui->setupUi(this);

  ui->lineLimitMax->setValidator(new QDoubleValidator(this));
  ui->lineLimitMin->setValidator(new QDoubleValidator(this));

  auto on_radio_toggled = [this](std::optional<PlotWidgetBase::CurveStyle> style, bool toggled) {
    if (toggled)
    {
      applyStyleToPlots(style);
    }
  };

  connect(ui->radioDefault, &QRadioButton::toggled, this,
          [=](bool toggled) { on_radio_toggled(std::nullopt, toggled); });
  connect(ui->radioPoints, &QRadioButton::toggled, this,
          [=](bool toggled) { on_radio_toggled(PlotWidgetBase::DOTS, toggled); });
  connect(ui->radioBoth, &QRadioButton::toggled, this,
          [=](bool toggled) { on_radio_toggled(PlotWidgetBase::LINES_AND_DOTS, toggled); });
  connect(ui->radioSteps, &QRadioButton::toggled, this,
          [=](bool toggled) { on_radio_toggled(PlotWidgetBase::STEPS, toggled); });
  connect(ui->radioStepsInv, &QRadioButton::toggled, this,
          [=](bool toggled) { on_radio_toggled(PlotWidgetBase::STEPSINV, toggled); });
  connect(ui->radioSticks, &QRadioButton::toggled, this,
          [=](bool toggled) { on_radio_toggled(PlotWidgetBase::STICKS, toggled); });

  connect(ui->comboBoxWidth, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
          this, &CurveStyleEditor::onComboWidthChanged);

  // Qt's itemClicked fires *after* the selection has changed, so a click on an
  // already-selected row would otherwise be a no-op. Track the previous click to
  // turn that second click into a deselection (toggle behavior).
  connect(ui->listWidget, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
    if (item->isSelected() && item == _last_clicked_item)
    {
      item->setSelected(false);
      _last_clicked_item = nullptr;
    }
    else
    {
      _last_clicked_item = item;
    }
  });

  disableWidgets();
}

CurveStyleEditor::~CurveStyleEditor()
{
  delete ui;
}

void CurveStyleEditor::setTargetPlots(const std::vector<PlotWidget*>& plots)
{
  disconnectFromPlots();
  _target_plots = plots;

  if (!_target_plots.empty())
  {
    connectToPlots();
    refreshFromPlots();
  }
  else
  {
    ui->listWidget->clear();
    _last_clicked_item = nullptr;
    _active_color_row = nullptr;
    _active_color_plot = nullptr;
    disableWidgets();
  }
}

const std::vector<PlotWidget*>& CurveStyleEditor::targetPlots() const
{
  return _target_plots;
}

void CurveStyleEditor::refreshFromPlots()
{
  if (_target_plots.empty())
  {
    return;
  }

  QSignalBlocker block_list(ui->listWidget);
  ui->listWidget->clear();
  _last_clicked_item = nullptr;
  // The row pointers from the previous build are gone, so any active swatch
  // target is stale — clear it. The popup will re-seed on the next click.
  _active_color_row = nullptr;
  _active_color_plot = nullptr;
  setupTable();

  syncControlsToActivePlot();

  if (ui->listWidget->count() != 0)
  {
    enableWidgets();
  }
  else
  {
    disableWidgets();
  }
}

void CurveStyleEditor::syncControlsToActivePlot()
{
  auto plots = activePlots();
  if (plots.empty())
  {
    return;
  }
  auto* primary = plots.front();

  QSignalBlocker block_rd(ui->radioDefault);
  QSignalBlocker block_rp(ui->radioPoints);
  QSignalBlocker block_rb(ui->radioBoth);
  QSignalBlocker block_rs(ui->radioSticks);
  QSignalBlocker block_rst(ui->radioSteps);
  QSignalBlocker block_rsi(ui->radioStepsInv);

  ui->radioDefault->setChecked(true);
  const auto overridden_style = primary->overriddenCurvesStyle();
  if (overridden_style.has_value())
  {
    switch (overridden_style.value())
    {
      case PlotWidgetBase::LINES:
        ui->radioDefault->setChecked(true);
        break;
      case PlotWidgetBase::DOTS:
        ui->radioPoints->setChecked(true);
        break;
      case PlotWidgetBase::LINES_AND_DOTS:
        ui->radioBoth->setChecked(true);
        break;
      case PlotWidgetBase::STICKS:
        ui->radioSticks->setChecked(true);
        break;
      case PlotWidgetBase::STEPS:
        ui->radioSteps->setChecked(true);
        break;
      case PlotWidgetBase::STEPSINV:
        ui->radioStepsInv->setChecked(true);
        break;
    }
  }

  QSignalBlocker block_width(ui->comboBoxWidth);
  ui->comboBoxWidth->setCurrentIndex(static_cast<int>(primary->lineWidth()));

  QSignalBlocker block_max(ui->checkBoxMax);
  QSignalBlocker block_min(ui->checkBoxMin);
  QSignalBlocker block_lmax(ui->lineLimitMax);
  QSignalBlocker block_lmin(ui->lineLimitMin);

  auto ylimits = primary->customAxisLimit();
  auto range_x = primary->getVisualizationRangeX();
  Range suggested_limits = primary->getVisualizationRangeY(range_x);

  if (ylimits.min != -MAX_DOUBLE)
  {
    ui->checkBoxMin->setChecked(true);
    ui->lineLimitMin->setEnabled(true);
    ui->lineLimitMin->setText(QString::number(ylimits.min));
  }
  else
  {
    ui->checkBoxMin->setChecked(false);
    ui->lineLimitMin->setEnabled(false);
    ui->lineLimitMin->setText(QString::number(suggested_limits.min));
  }

  if (ylimits.max != MAX_DOUBLE)
  {
    ui->checkBoxMax->setChecked(true);
    ui->lineLimitMax->setEnabled(true);
    ui->lineLimitMax->setText(QString::number(ylimits.max));
  }
  else
  {
    ui->checkBoxMax->setChecked(false);
    ui->lineLimitMax->setEnabled(false);
    ui->lineLimitMax->setText(QString::number(suggested_limits.max));
  }
}

void CurveStyleEditor::onColorClicked(EditorRowWidget* row)
{
  // Find the QListWidgetItem that owns this row to recover plot+curve_name.
  PlotWidget* plot = nullptr;
  QString curve_name;
  for (int i = 0; i < ui->listWidget->count(); ++i)
  {
    auto* item = ui->listWidget->item(i);
    if (ui->listWidget->itemWidget(item) == row)
    {
      plot = static_cast<PlotWidget*>(item->data(ROLE_PLOT_PTR).value<void*>());
      curve_name = item->data(ROLE_CURVE_NAME).toString();
      break;
    }
  }
  if (!plot)
  {
    return;
  }

  _active_color_row = row;
  _active_color_plot = plot;
  _active_color_curve = curve_name;

  if (!_color_picker_popup)
  {
    _color_picker_popup = new ColorPickerPopup(this);
    connect(_color_picker_popup, &ColorPickerPopup::colorChanged, this,
            &CurveStyleEditor::onPickerColorChanged);
  }

  _color_picker_popup->setColor(row->color());
  // Position the popup just below the swatch so it visually anchors to it.
  _color_picker_popup->move(row->mapToGlobal(QPoint(0, row->height())));
  _color_picker_popup->show();
}

void CurveStyleEditor::onPickerColorChanged(QColor c)
{
  if (!_active_color_row || !_active_color_plot)
  {
    return;
  }
  _active_color_row->setColor(c);
  _active_color_plot->on_changeCurveColor(_active_color_curve, c);
  emit _active_color_plot->undoableChange();
}

void CurveStyleEditor::setupTable()
{
  for (auto* plot : _target_plots)
  {
    std::map<QString, QColor> colors = plot->getCurveColors();

    for (auto& it : colors)
    {
      auto alias = it.first;
      auto color = it.second;
      auto item = new QListWidgetItem();
      item->setData(ROLE_CURVE_NAME, it.first);
      item->setData(ROLE_PLOT_PTR, QVariant::fromValue(static_cast<void*>(plot)));

      ui->listWidget->addItem(item);
      auto plot_row = new EditorRowWidget(alias, color);
      item->setSizeHint(plot_row->sizeHint());
      ui->listWidget->setItemWidget(item, plot_row);

      connect(plot_row, &EditorRowWidget::colorClicked, this, &CurveStyleEditor::onColorClicked);
    }
  }
}

void CurveStyleEditor::on_buttonDeleteCurve_clicked()
{
  auto selected = ui->listWidget->selectedItems();
  if (selected.size() != 1)
  {
    return;
  }
  auto* item = selected.front();
  auto* widget = ui->listWidget->itemWidget(item);
  QString curve = item->data(ROLE_CURVE_NAME).toString();
  auto* plot = static_cast<PlotWidget*>(item->data(ROLE_PLOT_PTR).value<void*>());

  if (item == _last_clicked_item)
  {
    _last_clicked_item = nullptr;
  }
  if (widget == _active_color_row)
  {
    _active_color_row = nullptr;
    _active_color_plot = nullptr;
  }

  delete ui->listWidget->takeItem(ui->listWidget->row(item));
  if (widget)
  {
    widget->deleteLater();
  }
  if (plot)
  {
    plot->removeCurve(curve);
    plot->replot();
    emit plot->undoableChange();
  }

  ui->buttonDeleteCurve->setEnabled(false);
  if (ui->listWidget->count() == 0)
  {
    disableWidgets();
  }
}

void CurveStyleEditor::disableWidgets()
{
  ui->frameLimits->setEnabled(false);
  ui->frameStyle->setEnabled(false);
  ui->comboBoxWidth->setEnabled(false);
  ui->buttonDeleteCurve->setEnabled(false);
}

void CurveStyleEditor::enableWidgets()
{
  ui->frameLimits->setEnabled(true);
  ui->frameStyle->setEnabled(true);
  ui->comboBoxWidth->setEnabled(true);
}

std::vector<PlotWidget*> CurveStyleEditor::activePlots() const
{
  auto selected = ui->listWidget->selectedItems();
  if (selected.size() == 1)
  {
    auto* plot = static_cast<PlotWidget*>(selected.front()->data(ROLE_PLOT_PTR).value<void*>());
    if (plot)
    {
      return { plot };
    }
  }
  return _target_plots;
}

void CurveStyleEditor::applyStyleToPlots(std::optional<PlotWidgetBase::CurveStyle> style)
{
  for (auto* plot : activePlots())
  {
    plot->overrideCurvesStyle(style);
    plot->updateCurvesStyle();
    emit plot->undoableChange();
  }
}

void CurveStyleEditor::updateLimits()
{
  if (_target_plots.empty())
  {
    return;
  }

  double ymin = -MAX_DOUBLE;
  double ymax = MAX_DOUBLE;

  if (ui->checkBoxMax->isChecked() && !ui->lineLimitMax->text().isEmpty())
  {
    bool ok = false;
    double val = ui->lineLimitMax->text().toDouble(&ok);
    if (ok)
    {
      ymax = val;
    }
  }

  if (ui->checkBoxMin->isChecked() && !ui->lineLimitMin->text().isEmpty())
  {
    bool ok = false;
    double val = ui->lineLimitMin->text().toDouble(&ok);
    if (ok)
    {
      ymin = val;
    }
  }

  if (ymin > ymax)
  {
    ui->lineLimitMin->setText(QString::number(ymax));
    ui->lineLimitMax->setText(QString::number(ymin));
    std::swap(ymin, ymax);
  }

  Range range;
  range.min = ymin;
  range.max = ymax;

  for (auto* plot : activePlots())
  {
    plot->setCustomAxisLimits(range);
    emit plot->undoableChange();
  }
}

void CurveStyleEditor::on_checkBoxMax_toggled(bool checked)
{
  ui->lineLimitMax->setEnabled(checked);
  updateLimits();
}

void CurveStyleEditor::on_checkBoxMin_toggled(bool checked)
{
  ui->lineLimitMin->setEnabled(checked);
  updateLimits();
}

void CurveStyleEditor::on_pushButtonReset_clicked()
{
  if (_target_plots.empty())
  {
    return;
  }

  Range no_limits;
  no_limits.min = -MAX_DOUBLE;
  no_limits.max = +MAX_DOUBLE;

  auto plots = activePlots();
  for (auto* plot : plots)
  {
    plot->setCustomAxisLimits(no_limits);
    emit plot->undoableChange();
  }

  auto range_x = plots.front()->getVisualizationRangeX();
  Range limits = plots.front()->getVisualizationRangeY(range_x);
  ui->lineLimitMin->setText(QString::number(limits.min));
  ui->lineLimitMax->setText(QString::number(limits.max));
}

void CurveStyleEditor::on_lineLimitMax_textChanged(const QString&)
{
  updateLimits();
}

void CurveStyleEditor::on_lineLimitMin_textChanged(const QString&)
{
  updateLimits();
}

void CurveStyleEditor::onComboWidthChanged(int index)
{
  for (auto* plot : activePlots())
  {
    plot->setLineWidth(static_cast<LineWidth>(index));
    emit plot->undoableChange();
  }
}

void CurveStyleEditor::on_listWidget_itemSelectionChanged()
{
  ui->buttonDeleteCurve->setEnabled(ui->listWidget->selectedItems().size() == 1);
  syncControlsToActivePlot();
}

void CurveStyleEditor::disconnectFromPlots()
{
  for (auto& conn : _connections)
  {
    QObject::disconnect(conn);
  }
  _connections.clear();
}

void CurveStyleEditor::connectToPlots()
{
  for (auto* plot : _target_plots)
  {
    _connections.push_back(
        connect(plot, &PlotWidget::curveListChanged, this, &CurveStyleEditor::refreshFromPlots));

    _connections.push_back(connect(plot, &QObject::destroyed, this, [this, plot]() {
      auto it = std::find(_target_plots.begin(), _target_plots.end(), plot);
      if (it != _target_plots.end())
      {
        _target_plots.erase(it);
      }
      if (plot == _active_color_plot)
      {
        _active_color_row = nullptr;
        _active_color_plot = nullptr;
      }
      if (_target_plots.empty())
      {
        ui->listWidget->clear();
        _last_clicked_item = nullptr;
        disableWidgets();
      }
      else
      {
        refreshFromPlots();
      }
    }));
  }
}
