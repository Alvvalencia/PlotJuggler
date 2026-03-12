#include "python_custom_function.h"

#include <QTextStream>

static std::once_flag g_py_once;

PythonCustomFunction::PythonCustomFunction(SnippetData snippet) : CustomFunction(snippet)
{
  initEngine();

  {
    QTextStream in(&snippet.global_vars);
    while (!in.atEnd())
    {
      in.readLine();
      global_lines_++;
    }
  }
  {
    QTextStream in(&snippet.function);
    while (!in.atEnd())
    {
      in.readLine();
      function_lines_++;
    }
  }
}

PythonCustomFunction::~PythonCustomFunction()
{
  std::unique_lock<std::mutex> lk(mutex_);

  if (_py_calc)
  {
    PyGILState_STATE gil = PyGILState_Ensure();
    Py_DECREF(_py_calc);
    _py_calc = nullptr;
    PyGILState_Release(gil);
  }
  if (_locals)
  {
    PyGILState_STATE gil = PyGILState_Ensure();
    Py_DECREF(_locals);
    _locals = nullptr;
    PyGILState_Release(gil);
  }
  if (_globals)
  {
    PyGILState_STATE gil = PyGILState_Ensure();
    Py_DECREF(_globals);
    _globals = nullptr;
    PyGILState_Release(gil);
  }
}

void PythonCustomFunction::ensurePythonInitialized()
{
  std::call_once(g_py_once, []() {
    Py_Initialize();
    PyEval_InitThreads();
    PyEval_SaveThread();
  });
}

std::string PythonCustomFunction::fetchPythonExceptionWithTraceback()
{
  PyObject* ptype = nullptr;
  PyObject* pvalue = nullptr;
  PyObject* ptrace = nullptr;

  PyErr_Fetch(&ptype, &pvalue, &ptrace);
  PyErr_NormalizeException(&ptype, &pvalue, &ptrace);

  PyObject* tbmod = PyImport_ImportModule("traceback");
  PyObject* fmt = tbmod ? PyObject_GetAttrString(tbmod, "format_exception") : nullptr;

  std::string out = "Python error";

  if (fmt && PyCallable_Check(fmt))
  {
    PyObject* list =
        PyObject_CallFunctionObjArgs(fmt, ptype ? ptype : Py_None, pvalue ? pvalue : Py_None,
                                     ptrace ? ptrace : Py_None, nullptr);
    if (list)
    {
      PyObject* joined = PyUnicode_Join(PyUnicode_FromString(""), list);
      if (joined)
      {
        const char* s = PyUnicode_AsUTF8(joined);
        if (s)
        {
          out = s;
        }
        Py_DECREF(joined);
      }
      Py_DECREF(list);
    }
  }
  else
  {
    if (pvalue)
    {
      PyObject* s = PyObject_Str(pvalue);
      if (s)
      {
        const char* c = PyUnicode_AsUTF8(s);
        if (c)
        {
          out = c;
        }
        Py_DECREF(s);
      }
    }
  }

  Py_XDECREF(fmt);
  Py_XDECREF(tbmod);

  Py_XDECREF(ptype);
  Py_XDECREF(pvalue);
  Py_XDECREF(ptrace);

  return out;
}

std::string PythonCustomFunction::formatError(const std::string& tb_text) const
{
  // Queremos algo parecido a Lua:
  // [Global]: line X: ...
  // [Function]: line Y: ...
  //
  // En Python, al usar exec() con filenames, el traceback suele incluir:
  //   File "<PJ_GLOBAL>", line N
  //   File "<PJ_FUNCTION>", line M
  //
  // Ajustamos line para function quitando la línea de "def calc(...):"
  // (igual que Lua hace -1).
  const bool is_function = (tb_text.find("<PJ_FUNCTION>") != std::string::npos);
  const char* tag = is_function ? "[Function]: line " : "[Global]: line ";

  auto pos = tb_text.find(is_function ? "<PJ_FUNCTION>" : "<PJ_GLOBAL>");
  if (pos == std::string::npos)
  {
    return tb_text;
  }

  auto line_pos = tb_text.find("line ", pos);
  if (line_pos == std::string::npos)
  {
    return tb_text;
  }
  line_pos += 5;

  int line_num = 0;
  while (line_pos < tb_text.size() && std::isdigit(tb_text[line_pos]))
  {
    line_num = line_num * 10 + (tb_text[line_pos] - '0');
    line_pos++;
  }

  if (is_function)
  {
    line_num -= 1;
    if (line_num < 1)
    {
      line_num = 1;
    }
  }

  // Mensaje: coge la última línea del traceback (normalmente "TypeError: ...")
  auto last_nl = tb_text.find_last_of('\n');
  std::string last_line = (last_nl == std::string::npos) ? tb_text : tb_text.substr(last_nl + 1);
  if (last_line.empty())
  {
    last_line = tb_text;
  }

  std::string out = tag + std::to_string(line_num) + ": " + last_line;
  return out;
}

static std::string validatePythonImports(const QString& code, const char* tag)
{
  QTextStream in(const_cast<QString*>(&code), QIODevice::ReadOnly);

  int line_no = 0;
  while (!in.atEnd())
  {
    QString line = in.readLine();
    line_no++;

    QString trimmed = line.trimmed();

    if (trimmed.startsWith("import "))
    {
      if (trimmed != "import math")
      {
        return QString("%1: line %2: only 'import math' is allowed")
            .arg(tag)
            .arg(line_no)
            .toStdString();
      }
    }

    if (trimmed.startsWith("from "))
    {
      return QString("%1: line %2: 'from ... import ...' is not allowed")
          .arg(tag)
          .arg(line_no)
          .toStdString();
    }
  }

  return "";
}

void PythonCustomFunction::initEngine()
{
  std::unique_lock<std::mutex> lk(mutex_);

  ensurePythonInitialized();
  PyGILState_STATE gil = PyGILState_Ensure();

  if (_py_calc)
  {
    Py_DECREF(_py_calc);
    _py_calc = nullptr;
  }
  if (_locals)
  {
    Py_DECREF(_locals);
    _locals = nullptr;
  }
  if (_globals)
  {
    Py_DECREF(_globals);
    _globals = nullptr;
  }

  _globals = PyDict_New();
  PyDict_SetItemString(_globals, "__builtins__", PyEval_GetBuiltins());

#ifdef PJ_HAS_NANOBIND
  PyObject* pj_module = PyImport_ImportModule("pj");
  if (pj_module)
  {
    PyDict_SetItemString(_globals, "pj", pj_module);
    Py_DECREF(pj_module);
  }
  else
  {
    PyErr_Clear();
  }
#endif

  _locals = _globals;
  Py_INCREF(_locals);

  // validar imports en global_vars
  std::string err = validatePythonImports(_snippet.global_vars, "[Global]");
  if (!err.empty())
  {
    PyGILState_Release(gil);
    throw std::runtime_error(err);
  }

  // 1) Ejecuta global_vars
  const std::string global_code = _snippet.global_vars.toStdString();
  if (!global_code.empty())
  {
    PyObject* r = PyRun_StringFlags(global_code.c_str(), Py_file_input, _globals, _locals, nullptr);
    if (!r)
    {
      std::string tb = fetchPythonExceptionWithTraceback();
      PyGILState_Release(gil);
      throw std::runtime_error(formatError(tb));
    }
    Py_DECREF(r);
  }

  // validar imports en la función
  err = validatePythonImports(_snippet.function, "[Function]");
  if (!err.empty())
  {
    PyGILState_Release(gil);
    throw std::runtime_error(err);
  }

  // 2) Construye def calc(time, value, v1..vN):
  std::string def = "def calc(time, value";
  for (int i = 1; i <= _snippet.additional_sources.size(); i++)
  {
    def += ", v" + std::to_string(i);
  }
  def += "):\n";

  // Indenta el cuerpo del usuario
  const std::string body = _snippet.function.toStdString();
  if (body.empty())
  {
    def += "    return float('nan')\n";
  }
  else
  {
    size_t start = 0;
    while (start < body.size())
    {
      size_t end = body.find('\n', start);
      if (end == std::string::npos)
      {
        end = body.size();
      }
      std::string line = body.substr(start, end - start);
      def += "    " + line + "\n";
      start = end + 1;
    }
  }

  // Ejecuta con filename controlado para poder parsear traceback
  PyObject* compiled = Py_CompileString(def.c_str(), "<PJ_FUNCTION>", Py_file_input);
  if (!compiled)
  {
    std::string tb = fetchPythonExceptionWithTraceback();
    PyGILState_Release(gil);
    throw std::runtime_error(formatError(tb));
  }

  PyObject* execres = PyEval_EvalCode(compiled, _globals, _locals);
  Py_DECREF(compiled);

  if (!execres)
  {
    std::string tb = fetchPythonExceptionWithTraceback();
    PyGILState_Release(gil);
    throw std::runtime_error(formatError(tb));
  }
  Py_DECREF(execres);

  PyObject* fn = PyDict_GetItemString(_locals, "calc");
  if (!fn)
  {
    fn = PyDict_GetItemString(_globals, "calc");
  }
  if (!fn || !PyCallable_Check(fn))
  {
    PyGILState_Release(gil);
    throw std::runtime_error("Python Engine: calc no es callable");
  }

  Py_INCREF(fn);
  _py_calc = fn;

  PyGILState_Release(gil);
}

void PythonCustomFunction::calculatePoints(const std::vector<const PlotData*>& src_data,
                                           size_t point_index, std::vector<PlotData::Point>& points)
{
  std::unique_lock<std::mutex> lk(mutex_);

  _chan_values.resize(src_data.size());

  const PlotData::Point& old_point = src_data.front()->at(point_index);

  for (size_t chan_index = 0; chan_index < src_data.size(); chan_index++)
  {
    double value;
    const PlotData* chan_data = src_data[chan_index];
    int index = chan_data->getIndexFromX(old_point.x);
    if (index != -1)
    {
      value = chan_data->at(index).y;
    }
    else
    {
      value = std::numeric_limits<double>::quiet_NaN();
    }
    _chan_values[chan_index] = value;
  }

  PyGILState_STATE gil = PyGILState_Ensure();

  if (!_py_calc)
  {
    PyGILState_Release(gil);
    throw std::runtime_error("Python Engine: calc no inicializado");
  }

  // build args: (time, value, v1..vN) donde value = v[0], v1=v[1]...
  const auto& v = _chan_values;

  PyObject* args = nullptr;

  auto make_tuple = [&](int n) -> PyObject* {
    PyObject* t = PyTuple_New(n);
    return t;
  };

  const int add = (int)_snippet.additional_sources.size();
  if (add < 0 || add > 8)
  {
    PyGILState_Release(gil);
    throw std::runtime_error("Python Engine: maximum number of additional data sources is 8");
  }

  const int nargs = 2 + add;
  args = make_tuple(nargs);

  PyTuple_SetItem(args, 0, PyFloat_FromDouble(old_point.x));
  PyTuple_SetItem(args, 1, PyFloat_FromDouble(v[0]));
  for (int i = 0; i < add; i++)
  {
    PyTuple_SetItem(args, 2 + i, PyFloat_FromDouble(v[1 + i]));
  }

  PyObject* result = PyObject_CallObject(_py_calc, args);
  Py_DECREF(args);

  if (!result)
  {
    std::string tb = fetchPythonExceptionWithTraceback();
    PyGILState_Release(gil);
    throw std::runtime_error(formatError(tb));
  }

  points.clear();

  // Semántica igual que Lua:
  // - (time, value)
  // - number
  // - list/tuple of pairs
  if (PyTuple_Check(result) && PyTuple_Size(result) == 2)
  {
    PyObject* rx = PyTuple_GetItem(result, 0);
    PyObject* ry = PyTuple_GetItem(result, 1);

    PlotData::Point p;
    p.x = PyFloat_AsDouble(rx);
    p.y = PyFloat_AsDouble(ry);
    points.push_back(p);
    Py_DECREF(result);
    PyGILState_Release(gil);
    return;
  }

  if (PyFloat_Check(result) || PyLong_Check(result))
  {
    PlotData::Point p;
    p.x = old_point.x;
    p.y = PyFloat_AsDouble(result);
    points.push_back(p);
    Py_DECREF(result);
    PyGILState_Release(gil);
    return;
  }

  if (PyList_Check(result) || PyTuple_Check(result))
  {
    const Py_ssize_t len = PySequence_Size(result);
    for (Py_ssize_t i = 0; i < len; i++)
    {
      PyObject* item = PySequence_GetItem(result, i);
      if (!item)
      {
        Py_DECREF(result);
        std::string tb = fetchPythonExceptionWithTraceback();
        PyGILState_Release(gil);
        throw std::runtime_error(formatError(tb));
      }

      if (!(PyTuple_Check(item) && PyTuple_Size(item) == 2) &&
          !(PyList_Check(item) && PyList_Size(item) == 2))
      {
        Py_DECREF(item);
        Py_DECREF(result);
        PyGILState_Release(gil);
        throw std::runtime_error("Wrong return object: expecting either a single value, "
                                 "two values (time, value) "
                                 "or an array of two-sized arrays (time, value)");
      }

      PyObject* rx = PySequence_GetItem(item, 0);
      PyObject* ry = PySequence_GetItem(item, 1);

      PlotData::Point p;
      p.x = PyFloat_AsDouble(rx);
      p.y = PyFloat_AsDouble(ry);

      Py_DECREF(rx);
      Py_DECREF(ry);
      Py_DECREF(item);

      points.push_back(p);
    }

    Py_DECREF(result);
    PyGILState_Release(gil);
    return;
  }

  Py_DECREF(result);
  PyGILState_Release(gil);
  throw std::runtime_error("Wrong return object: expecting either a single value, "
                           "two values (time, value) "
                           "or an array of two-sized arrays (time, value)");
}

bool PythonCustomFunction::xmlLoadState(const QDomElement& parent_element)
{
  bool ret = CustomFunction::xmlLoadState(parent_element);
  initEngine();
  return ret;
}
