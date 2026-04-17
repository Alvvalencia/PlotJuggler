/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "curve_style_editor.h"
#include "ui_curve_style_editor.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QListWidgetItem>
#include <QDoubleValidator>
#include <algorithm>
#include <limits>

const double MAX_DOUBLE = std::numeric_limits<double>::max() / 2;

// ---------------------------------------------------------------------------
// EditorRowWidget
// ---------------------------------------------------------------------------

EditorRowWidget::EditorRowWidget(QString text, QColor color) : QWidget()
{
  setMouseTracking(true);
  const QSize button_size(20, 20);

  auto layout = new QHBoxLayout();
  setLayout(layout);
  _text = new QLabel(text, this);

  _empty_spacer = new QWidget();
  _empty_spacer->setFixedSize(button_size);

  setColor(color);
  _delete_button = new QPushButton(this);
  _delete_button->setFlat(true);
  _delete_button->setFixedSize(button_size);
  auto icon = QIcon(":/resources/svg/trash.svg");
  _delete_button->setStyleSheet("QPushButton:hover{ border: 0px;}");

  _delete_button->setIcon(icon);
  _delete_button->setIconSize(button_size);

  layout->addWidget(_empty_spacer);
  layout->addWidget(_delete_button);
  layout->addWidget(_text);

  _delete_button->setHidden(true);

  connect(_delete_button, &QPushButton::clicked, this, [this]() { emit deleteRow(this); });
}

void EditorRowWidget::enterEvent(QEvent*)
{
  _delete_button->setHidden(false);
  _empty_spacer->setHidden(true);
}

void EditorRowWidget::leaveEvent(QEvent*)
{
  _delete_button->setHidden(true);
  _empty_spacer->setHidden(false);
}

QString EditorRowWidget::text() const
{
  return _text->text();
}

void EditorRowWidget::setColor(QColor color)
{
  setStyleSheet(QString("color: %1;").arg(color.name()));
  _color = color;
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

  setupColorWidget();

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

  // Refresh always lands with no row selected. Color is per-curve and needs
  // an unambiguous target, so keep the color section off until the user picks
  // a row — the selection-changed slot will re-enable it.
  ui->widgetColor->setEnabled(false);
  ui->editColorText->setText("#000000");
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

void CurveStyleEditor::onColorChanged(QColor c)
{
  auto selected = ui->listWidget->selectedItems();
  if (selected.size() != 1)
  {
    return;
  }

  auto item = selected.front();
  auto row_widget = dynamic_cast<EditorRowWidget*>(ui->listWidget->itemWidget(item));
  auto* plot = static_cast<PlotWidget*>(item->data(ROLE_PLOT_PTR).value<void*>());
  if (row_widget && plot)
  {
    if (row_widget->color() != c)
    {
      row_widget->setColor(c);
    }
    plot->on_changeCurveColor(item->data(ROLE_CURVE_NAME).toString(), c);
    emit plot->undoableChange();
  }
}

void CurveStyleEditor::setupColorWidget()
{
  auto wheel_layout = new QVBoxLayout();
  wheel_layout->setMargin(0);
  wheel_layout->setSpacing(5);
  ui->widgetWheel->setLayout(wheel_layout);

  _color_wheel = new color_widgets::ColorWheel(this);
  wheel_layout->addWidget(_color_wheel);

  _color_preview = new color_widgets::ColorPreview(this);
  _color_preview->setMaximumHeight(25);
  wheel_layout->addWidget(_color_preview);

  connect(_color_wheel, &color_widgets::ColorWheel::colorChanged, this,
          &CurveStyleEditor::onColorChanged);

  connect(_color_wheel, &color_widgets::ColorWheel::colorChanged, _color_preview,
          &color_widgets::ColorPreview::setColor);

  connect(_color_wheel, &color_widgets::ColorWheel::colorChanged, this, [this](QColor col) {
    QSignalBlocker block(ui->editColorText);
    ui->editColorText->setText(col.name());
  });

  _color_wheel->setColor(Qt::blue);
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

      connect(plot_row, &EditorRowWidget::deleteRow, this, [this](QWidget* w) { onDeleteRow(w); });
    }
  }
}

void CurveStyleEditor::onDeleteRow(QWidget* w)
{
  int row_count = ui->listWidget->count();
  for (int row = 0; row < row_count; row++)
  {
    auto item = ui->listWidget->item(row);
    auto widget = ui->listWidget->itemWidget(item);
    if (widget == w)
    {
      QString curve = dynamic_cast<EditorRowWidget*>(w)->text();
      auto* plot = static_cast<PlotWidget*>(item->data(ROLE_PLOT_PTR).value<void*>());

      if (item == _last_clicked_item)
      {
        _last_clicked_item = nullptr;
      }
      delete ui->listWidget->takeItem(row);
      if (plot)
      {
        plot->removeCurve(curve);
        plot->replot();
        emit plot->undoableChange();
      }
      widget->deleteLater();
      row_count--;
      break;
    }
  }

  if (row_count == 0)
  {
    disableWidgets();
  }
}

void CurveStyleEditor::disableWidgets()
{
  ui->widgetColor->setEnabled(false);
  _color_wheel->setEnabled(false);
  _color_preview->setEnabled(false);
  ui->frameLimits->setEnabled(false);
  ui->frameStyle->setEnabled(false);
}

void CurveStyleEditor::enableWidgets()
{
  ui->widgetColor->setEnabled(true);
  _color_wheel->setEnabled(true);
  _color_preview->setEnabled(true);
  ui->frameLimits->setEnabled(true);
  ui->frameStyle->setEnabled(true);
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

void CurveStyleEditor::on_editColorText_textChanged(const QString& text)
{
  if (text.size() == 7 && text[0] == '#' && QColor::isValidColor(text))
  {
    QColor col(text);
    _color_wheel->setColor(col);
    _color_preview->setColor(col);
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
  auto selected = ui->listWidget->selectedItems();
  if (selected.size() == 0 || ui->listWidget->count() == 0)
  {
    ui->widgetColor->setEnabled(false);
    ui->editColorText->setText("#000000");
    syncControlsToActivePlot();
    return;
  }

  ui->widgetColor->setEnabled(true);

  if (selected.size() != 1)
  {
    return;
  }

  auto item = selected.front();
  auto row_widget = dynamic_cast<EditorRowWidget*>(ui->listWidget->itemWidget(item));
  if (row_widget)
  {
    QSignalBlocker block(_color_wheel);
    QSignalBlocker block2(_color_preview);
    QSignalBlocker block3(ui->editColorText);
    _color_wheel->setColor(row_widget->color());
    _color_preview->setColor(row_widget->color());
    ui->editColorText->setText(row_widget->color().name());
  }

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
