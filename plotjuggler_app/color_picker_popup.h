/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef COLOR_PICKER_POPUP_H
#define COLOR_PICKER_POPUP_H

#include <QDialog>
#include <QWidget>
#include <QColor>

class QLineEdit;

// Horizontal hue strip with two handle markers above/below the rainbow.
// Emits hueChanged on drag; setHue suppresses the signal.
class HueSlider : public QWidget
{
  Q_OBJECT
public:
  explicit HueSlider(QWidget* parent = nullptr);

  int hue() const
  {
    return _hue;
  }
  void setHue(int hue);

  QSize sizeHint() const override;

signals:
  void hueChanged(int hue);

protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;

private:
  void updateFromMouseX(int x);
  int _hue = 0;  // [0, 359]
};

// Saturation/value square for the current hue. Background is repainted when
// the hue changes; the cursor is a hollow white circle at (s*w, (1-v)*h).
class SVSquare : public QWidget
{
  Q_OBJECT
public:
  explicit SVSquare(QWidget* parent = nullptr);

  void setHue(int hue);
  void setSV(qreal s, qreal v);

  QColor color() const;

  QSize sizeHint() const override;

signals:
  void colorChanged(QColor color);

protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;

private:
  void updateFromMouseXY(int x, int y);
  int _hue = 0;
  qreal _s = 1.0;
  qreal _v = 1.0;
};

// Modern minimal popup: HueSlider + SVSquare + hex line edit, stacked.
// Inherits QDialog so the application's "QDialog { background: ... }" theme
// rule applies (a plain QWidget would be transparent per the global theme).
// Constructed with Qt::Popup window flag — click outside or Escape closes it.
class ColorPickerPopup : public QDialog
{
  Q_OBJECT
public:
  explicit ColorPickerPopup(QWidget* parent = nullptr);

  void setColor(const QColor& c);
  QColor color() const;

signals:
  void colorChanged(QColor c);

private:
  void onHueChanged(int hue);
  void onSquareChanged(QColor c);
  void onHexCommitted();
  void recomputeAndEmit();
  void pushToHexField(const QColor& c);

  HueSlider* _hue_slider;
  SVSquare* _sv_square;
  QLineEdit* _hex_edit;

  int _hue = 0;
  qreal _s = 1.0;
  qreal _v = 1.0;
};

#endif  // COLOR_PICKER_POPUP_H
