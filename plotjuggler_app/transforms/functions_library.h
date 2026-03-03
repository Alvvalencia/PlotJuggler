#ifndef FUNCTIONS_LIBRARY_H
#define FUNCTIONS_LIBRARY_H

#include <QDialog>

namespace Ui
{
class FunctionsLibrary;
}

class functions_library : public QDialog
{
  Q_OBJECT

public:
  explicit functions_library(QWidget* parent = nullptr);
  ~functions_library();

private:
  Ui::FunctionsLibrary* ui;
};

#endif  // FUNCTIONS_LIBRARY_H
