/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "color_picker_popup.h"

#include <QPainter>
#include <QMouseEvent>
#include <QLinearGradient>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QRegExpValidator>
#include <QSignalBlocker>
#include <algorithm>

namespace
{
constexpr int HUE_SLIDER_HEIGHT = 18;
constexpr int SQUARE_SIDE = 220;
constexpr int CURSOR_RADIUS = 6;
constexpr int POPUP_MARGIN = 8;
}  // namespace

// ---------------------------------------------------------------------------
// HueSlider
// ---------------------------------------------------------------------------

HueSlider::HueSlider(QWidget* parent) : QWidget(parent)
{
  setFixedHeight(HUE_SLIDER_HEIGHT);
  setCursor(Qt::PointingHandCursor);
}

QSize HueSlider::sizeHint() const
{
  return QSize(SQUARE_SIDE, HUE_SLIDER_HEIGHT);
}

void HueSlider::setHue(int hue)
{
  hue = qBound(0, hue, 359);
  if (_hue == hue)
  {
    return;
  }
  _hue = hue;
  update();
}

void HueSlider::paintEvent(QPaintEvent*)
{
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);

  const int w = width();
  const int h = height();

  QLinearGradient grad(0, 0, w, 0);
  for (int i = 0; i <= 6; ++i)
  {
    grad.setColorAt(i / 6.0, QColor::fromHsv(i * 60 % 360, 255, 255));
  }
  p.fillRect(rect(), grad);

  // Handle: a thin vertical bar at the current hue position, drawn with a
  // white core and a black outline so it stays visible against any rainbow
  // segment.
  const int x = qBound(0, int(qreal(_hue) / 359.0 * (w - 1)), w - 1);
  p.setPen(QPen(Qt::black, 1));
  p.drawLine(x - 2, 0, x - 2, h - 1);
  p.drawLine(x + 2, 0, x + 2, h - 1);
  p.setPen(QPen(Qt::white, 2));
  p.drawLine(x, 0, x, h - 1);
}

void HueSlider::mousePressEvent(QMouseEvent* event)
{
  if (event->button() == Qt::LeftButton)
  {
    updateFromMouseX(event->x());
  }
}

void HueSlider::mouseMoveEvent(QMouseEvent* event)
{
  if (event->buttons() & Qt::LeftButton)
  {
    updateFromMouseX(event->x());
  }
}

void HueSlider::updateFromMouseX(int x)
{
  const int w = width();
  if (w <= 1)
  {
    return;
  }
  int hue = int(qreal(qBound(0, x, w - 1)) / qreal(w - 1) * 359.0);
  if (hue == _hue)
  {
    return;
  }
  _hue = hue;
  update();
  emit hueChanged(_hue);
}

// ---------------------------------------------------------------------------
// SVSquare
// ---------------------------------------------------------------------------

SVSquare::SVSquare(QWidget* parent) : QWidget(parent)
{
  setMinimumSize(SQUARE_SIDE, SQUARE_SIDE);
  setCursor(Qt::CrossCursor);
}

QSize SVSquare::sizeHint() const
{
  return QSize(SQUARE_SIDE, SQUARE_SIDE);
}

void SVSquare::setHue(int hue)
{
  hue = qBound(0, hue, 359);
  if (_hue == hue)
  {
    return;
  }
  _hue = hue;
  update();
}

void SVSquare::setSV(qreal s, qreal v)
{
  _s = qBound(0.0, s, 1.0);
  _v = qBound(0.0, v, 1.0);
  update();
}

QColor SVSquare::color() const
{
  return QColor::fromHsvF(qreal(_hue) / 360.0, _s, _v);
}

void SVSquare::paintEvent(QPaintEvent*)
{
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);

  const int w = width();
  const int h = height();

  // Background: pure hue. Then white→transparent left-to-right (saturation),
  // then transparent→black top-to-bottom (value). The standard SV square.
  p.fillRect(rect(), QColor::fromHsv(_hue, 255, 255));

  QLinearGradient sat(0, 0, w, 0);
  sat.setColorAt(0.0, Qt::white);
  sat.setColorAt(1.0, QColor(255, 255, 255, 0));
  p.fillRect(rect(), sat);

  QLinearGradient val(0, 0, 0, h);
  val.setColorAt(0.0, QColor(0, 0, 0, 0));
  val.setColorAt(1.0, Qt::black);
  p.fillRect(rect(), val);

  // Cursor: hollow white circle with a thin black outline for contrast.
  const qreal cx = _s * (w - 1);
  const qreal cy = (1.0 - _v) * (h - 1);
  p.setBrush(Qt::NoBrush);
  p.setPen(QPen(Qt::black, 1));
  p.drawEllipse(QPointF(cx, cy), CURSOR_RADIUS + 1, CURSOR_RADIUS + 1);
  p.setPen(QPen(Qt::white, 2));
  p.drawEllipse(QPointF(cx, cy), CURSOR_RADIUS, CURSOR_RADIUS);
}

void SVSquare::mousePressEvent(QMouseEvent* event)
{
  if (event->button() == Qt::LeftButton)
  {
    updateFromMouseXY(event->x(), event->y());
  }
}

void SVSquare::mouseMoveEvent(QMouseEvent* event)
{
  if (event->buttons() & Qt::LeftButton)
  {
    updateFromMouseXY(event->x(), event->y());
  }
}

void SVSquare::updateFromMouseXY(int x, int y)
{
  const int w = width();
  const int h = height();
  if (w <= 1 || h <= 1)
  {
    return;
  }
  qreal s = qBound(0.0, qreal(x) / qreal(w - 1), 1.0);
  qreal v = qBound(0.0, 1.0 - qreal(y) / qreal(h - 1), 1.0);
  if (qFuzzyCompare(s, _s) && qFuzzyCompare(v, _v))
  {
    return;
  }
  _s = s;
  _v = v;
  update();
  emit colorChanged(color());
}

// ---------------------------------------------------------------------------
// ColorPickerPopup
// ---------------------------------------------------------------------------

ColorPickerPopup::ColorPickerPopup(QWidget* parent) : QDialog(parent)
{
  setWindowFlags(Qt::Popup);

  _hue_slider = new HueSlider(this);
  _sv_square = new SVSquare(this);
  _hex_edit = new QLineEdit(this);
  _hex_edit->setPlaceholderText("#rrggbb");
  _hex_edit->setMaxLength(7);
  _hex_edit->setValidator(new QRegExpValidator(QRegExp("#?[0-9a-fA-F]{0,6}"), _hex_edit));

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(POPUP_MARGIN, POPUP_MARGIN, POPUP_MARGIN, POPUP_MARGIN);
  layout->setSpacing(POPUP_MARGIN);
  layout->addWidget(_hue_slider);
  layout->addWidget(_sv_square, 1);
  layout->addWidget(_hex_edit);

  connect(_hue_slider, &HueSlider::hueChanged, this, &ColorPickerPopup::onHueChanged);
  connect(_sv_square, &SVSquare::colorChanged, this, &ColorPickerPopup::onSquareChanged);
  connect(_hex_edit, &QLineEdit::editingFinished, this, &ColorPickerPopup::onHexCommitted);
}

QColor ColorPickerPopup::color() const
{
  return QColor::fromHsvF(qreal(_hue) / 360.0, _s, _v);
}

void ColorPickerPopup::setColor(const QColor& c)
{
  if (!c.isValid())
  {
    return;
  }
  qreal h = c.hsvHueF();
  // Achromatic colors (black, white, grays) report hue == -1; keep the old
  // hue so the picker doesn't snap back to red on grayscale input.
  if (h >= 0)
  {
    _hue = qBound(0, int(h * 360.0), 359);
  }
  _s = c.hsvSaturationF();
  _v = c.valueF();

  QSignalBlocker bh(_hue_slider);
  QSignalBlocker bsv(_sv_square);
  QSignalBlocker bhex(_hex_edit);
  _hue_slider->setHue(_hue);
  _sv_square->setHue(_hue);
  _sv_square->setSV(_s, _v);
  pushToHexField(c);
}

void ColorPickerPopup::onHueChanged(int hue)
{
  _hue = hue;
  QSignalBlocker bsv(_sv_square);
  _sv_square->setHue(hue);
  recomputeAndEmit();
}

void ColorPickerPopup::onSquareChanged(QColor c)
{
  // Square gives us a full color; pull S/V back from it (H is already _hue).
  _s = c.hsvSaturationF();
  _v = c.valueF();
  recomputeAndEmit();
}

void ColorPickerPopup::onHexCommitted()
{
  QString txt = _hex_edit->text().trimmed();
  if (!txt.startsWith('#'))
  {
    txt.prepend('#');
  }
  if (txt.size() != 7)
  {
    // Refuse partial input; restore display from current state.
    pushToHexField(color());
    return;
  }
  QColor c(txt);
  if (!c.isValid())
  {
    pushToHexField(color());
    return;
  }
  setColor(c);
  emit colorChanged(c);
}

void ColorPickerPopup::recomputeAndEmit()
{
  QColor c = color();
  pushToHexField(c);
  emit colorChanged(c);
}

void ColorPickerPopup::pushToHexField(const QColor& c)
{
  QSignalBlocker bhex(_hex_edit);
  _hex_edit->setText(c.name());
}
