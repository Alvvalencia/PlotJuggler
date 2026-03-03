#include "functions_library.h"
#include "ui_functions_library.h"

functions_library::functions_library(QWidget* parent)
  : QDialog(parent), ui(new Ui::FunctionsLibrary)
{
  ui->setupUi(this);
}

functions_library::~functions_library()
{
  delete ui;
}
