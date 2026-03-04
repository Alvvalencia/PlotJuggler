#ifndef PYTHON_CUSTOM_FUNCTION_H
#define PYTHON_CUSTOM_FUNCTION_H

#include "custom_function.h"

#pragma push_macro("slots")
#undef slots
#pragma push_macro("signals")
#undef signals
#define PY_SSIZE_T_CLEAN
#include <Python.h>
#pragma pop_macro("signals")
#pragma pop_macro("slots")

#include <mutex>
#include <vector>
#include <string>

class PythonCustomFunction : public CustomFunction
{
public:
  PythonCustomFunction(SnippetData snippet = {});
  ~PythonCustomFunction() override;

  void initEngine() override;

  void calculatePoints(const std::vector<const PlotData*>& src_data, size_t point_index,
                       std::vector<PlotData::Point>& points) override;

  QString language() const override
  {
    return "PYTHON";
  }

  const char* name() const override
  {
    return "PythonCustomFunction";
  }

  bool xmlLoadState(const QDomElement& parent_element) override;

private:
  static void ensurePythonInitialized();

  std::string fetchPythonExceptionWithTraceback();
  std::string formatError(const std::string& tb_text) const;

  PyObject* _globals = nullptr;
  PyObject* _locals = nullptr;
  PyObject* _py_calc = nullptr;

  std::vector<double> _chan_values;
  std::mutex mutex_;

  int global_lines_ = 0;
  int function_lines_ = 0;
};

#endif
