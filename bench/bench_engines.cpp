#include <benchmark/benchmark.h>

#include "../plotjuggler_app/transforms/lua_custom_function.h"
#include "../plotjuggler_app/transforms/python_custom_function.h"
#include "../plotjuggler_base/include/PlotJuggler/plotdata.h"

#include <cmath>
#include <string>
#include <vector>

static void FillSeriesLinear(PlotData& dst, int N, double t0, double dt, double v0)
{
  dst.clear();
  for (int i = 0; i < N; i++)
  {
    double t = t0 + i * dt;
    double v = v0 + 0.001 * i;
    dst.pushBack(PJ::PlotData::Point(t, v));
  }
}

static void FillSeriesSine(PlotData& dst, int N, double t0, double dt, double freq_hz, double amp)
{
  dst.clear();
  const double w = 2.0 * M_PI * freq_hz;
  for (int i = 0; i < N; i++)
  {
    double t = t0 + i * dt;
    double v = amp * std::sin(w * t);
    dst.pushBack(PJ::PlotData::Point(t, v));
  }
}

static void FillSeriesQuatComponent(PlotData& dst, int N, double t0, double dt, int comp)
{
  dst.clear();
  for (int i = 0; i < N; i++)
  {
    double t = t0 + i * dt;

    double yaw = 0.25 * std::sin(2.0 * M_PI * 0.5 * t);
    double pitch = 0.20 * std::sin(2.0 * M_PI * 0.3 * t);
    double roll = 0.15 * std::sin(2.0 * M_PI * 0.2 * t);

    double cy = std::cos(yaw * 0.5);
    double sy = std::sin(yaw * 0.5);
    double cp = std::cos(pitch * 0.5);
    double sp = std::sin(pitch * 0.5);
    double cr = std::cos(roll * 0.5);
    double sr = std::sin(roll * 0.5);

    double qw = cr * cp * cy + sr * sp * sy;
    double qx = sr * cp * cy - cr * sp * sy;
    double qy = cr * sp * cy + sr * cp * sy;
    double qz = cr * cp * sy - sr * sp * cy;

    double v = 0.0;
    if (comp == 0)
    {
      v = qx;
    }
    else if (comp == 1)
    {
      v = qy;
    }
    else if (comp == 2)
    {
      v = qz;
    }
    else
    {
      v = qw;
    }

    dst.pushBack(PJ::PlotData::Point(t, v));
  }
}

static void BuildInput_Cheap(PlotDataMapRef& map, int N, int extra_sources)
{
  auto& base = map.getOrCreateNumeric("base");
  FillSeriesLinear(base, N, 0.0, 0.001, 1.0);

  for (int i = 1; i <= extra_sources; i++)
  {
    auto name = std::string("s") + std::to_string(i);
    auto& s = map.getOrCreateNumeric(name);
    FillSeriesLinear(s, N, 0.0, 0.001, double(i));
  }
}

static void BuildInput_Trig(PlotDataMapRef& map, int N, int extra_sources)
{
  auto& base = map.getOrCreateNumeric("base");
  FillSeriesSine(base, N, 0.0, 0.001, 3.0, 1.0);

  for (int i = 1; i <= extra_sources; i++)
  {
    auto name = std::string("s") + std::to_string(i);
    auto& s = map.getOrCreateNumeric(name);
    FillSeriesSine(s, N, 0.0, 0.001, 1.0 + 0.2 * i, 0.5);
  }
}

static void BuildInput_Quat(PlotDataMapRef& map, int N)
{
  auto& qx = map.getOrCreateNumeric("base");
  FillSeriesQuatComponent(qx, N, 0.0, 0.001, 0);

  auto& qy = map.getOrCreateNumeric("s1");
  FillSeriesQuatComponent(qy, N, 0.0, 0.001, 1);

  auto& qz = map.getOrCreateNumeric("s2");
  FillSeriesQuatComponent(qz, N, 0.0, 0.001, 2);

  auto& qw = map.getOrCreateNumeric("s3");
  FillSeriesQuatComponent(qw, N, 0.0, 0.001, 3);
}

static SnippetData MakeSnippetLua_Cheap(int extra_sources)
{
  SnippetData sn;
  sn.alias_name = "out";
  sn.linked_source = "base";
  sn.global_vars = "";

  sn.additional_sources.clear();
  for (int i = 1; i <= extra_sources; i++)
  {
    sn.additional_sources.push_back(QString("s%1").arg(i));
  }

  if (extra_sources >= 1)
  {
    sn.function = "if v1 == v1 then return value + v1 end\n"
                  "return value\n";
  }
  else
  {
    sn.function = "return value\n";
  }
  return sn;
}

static SnippetData MakeSnippetPy_Cheap(int extra_sources)
{
  SnippetData sn;
  sn.alias_name = "out";
  sn.linked_source = "base";
  sn.global_vars = "";

  sn.additional_sources.clear();
  for (int i = 1; i <= extra_sources; i++)
  {
    sn.additional_sources.push_back(QString("s%1").arg(i));
  }

  if (extra_sources >= 1)
  {
    sn.function = "if v1 == v1:\n"
                  "    return value + v1\n"
                  "return value\n";
  }
  else
  {
    sn.function = "return value\n";
  }
  return sn;
}

static SnippetData MakeSnippetLua_Trig(int extra_sources)
{
  SnippetData sn;
  sn.alias_name = "out";
  sn.linked_source = "base";
  sn.global_vars = "";  // <- importante

  sn.additional_sources.clear();
  for (int i = 1; i <= extra_sources; i++)
  {
    sn.additional_sources.push_back(QString("s%1").arg(i));
  }

  if (extra_sources >= 1)
  {
    sn.function = "local w = 2*math.pi*3.0\n"
                  "local s = math.sin(w*time)\n"
                  "local c = math.cos(w*time)\n"
                  "if v1 == v1 then return value + 0.7*v1 + 0.2*s + 0.1*c end\n"
                  "return value + 0.2*s + 0.1*c\n";
  }
  else
  {
    sn.function = "local w = 2*math.pi*3.0\n"
                  "local s = math.sin(w*time)\n"
                  "local c = math.cos(w*time)\n"
                  "return value + 0.2*s + 0.1*c\n";
  }
  return sn;
}

static SnippetData MakeSnippetPy_Trig(int extra_sources)
{
  SnippetData sn;
  sn.alias_name = "out";
  sn.linked_source = "base";
  sn.global_vars = "";  // <- importante: no dependemos de global_vars

  sn.additional_sources.clear();
  for (int i = 1; i <= extra_sources; i++)
  {
    sn.additional_sources.push_back(QString("s%1").arg(i));
  }

  if (extra_sources >= 1)
  {
    sn.function = "import math\n"
                  "w = 2*math.pi*3.0\n"
                  "s = math.sin(w*time)\n"
                  "c = math.cos(w*time)\n"
                  "if v1 == v1:\n"
                  "    return value + 0.7*v1 + 0.2*s + 0.1*c\n"
                  "return value + 0.2*s + 0.1*c\n";
  }
  else
  {
    sn.function = "import math\n"
                  "w = 2*math.pi*3.0\n"
                  "s = math.sin(w*time)\n"
                  "c = math.cos(w*time)\n"
                  "return value + 0.2*s + 0.1*c\n";
  }
  return sn;
}

static SnippetData MakeSnippetLua_QuatYaw()
{
  SnippetData sn;
  sn.alias_name = "out";
  sn.linked_source = "base";
  sn.additional_sources = QStringList{ "s1", "s2", "s3" };
  sn.global_vars = "";

  sn.function = "local qx = value\n"
                "local qy = v1\n"
                "local qz = v2\n"
                "local qw = v3\n"
                "if qy == nil or qz == nil or qw == nil then return 0/0 end\n"
                "local n2 = qx*qx + qy*qy + qz*qz + qw*qw\n"
                "if n2 < 1e-12 then return 0/0 end\n"
                "local inv = 1.0 / math.sqrt(n2)\n"
                "qx = qx*inv; qy = qy*inv; qz = qz*inv; qw = qw*inv\n"
                "local t0 = 2.0*(qw*qz + qx*qy)\n"
                "local t1 = 1.0 - 2.0*(qy*qy + qz*qz)\n"
                "return math.atan2(t0, t1)\n";

  return sn;
}

static SnippetData MakeSnippetPy_QuatYaw()
{
  SnippetData sn;
  sn.alias_name = "out";
  sn.linked_source = "base";
  sn.additional_sources = QStringList{ "s1", "s2", "s3" };
  sn.global_vars = "";

  sn.function = "import math\n"
                "qx = value\n"
                "qy = v1\n"
                "qz = v2\n"
                "qw = v3\n"
                "n2 = qx*qx + qy*qy + qz*qz + qw*qw\n"
                "if n2 < 1e-12:\n"
                "    return float('nan')\n"
                "inv = 1.0 / math.sqrt(n2)\n"
                "qx *= inv; qy *= inv; qz *= inv; qw *= inv\n"
                "t0 = 2.0*(qw*qz + qx*qy)\n"
                "t1 = 1.0 - 2.0*(qy*qy + qz*qz)\n"
                "return math.atan2(t0, t1)\n";

  return sn;
}

static void RunCalculate_Lua(benchmark::State& st, const SnippetData& sn, PlotDataMapRef& map,
                             PlotData& out)
{
  std::vector<PlotData*> outputs = { &out };

  LuaCustomFunction fn(sn);
  fn.initEngine();
  fn.setData(&map, {}, outputs);

  for (auto _ : st)
  {
    out.clear();
    try
    {
      fn.calculate();
    }
    catch (const std::exception& e)
    {
      st.SkipWithError(e.what());
      break;
    }
    benchmark::DoNotOptimize(out);
  }

  st.SetItemsProcessed((int64_t)st.iterations() * (int64_t)out.size());
}

static void RunCalculate_Py(benchmark::State& st, const SnippetData& sn, PlotDataMapRef& map,
                            PlotData& out)
{
  std::vector<PlotData*> outputs = { &out };

  PythonCustomFunction fn(sn);
  fn.initEngine();
  fn.setData(&map, {}, outputs);

  for (auto _ : st)
  {
    out.clear();
    try
    {
      fn.calculate();
    }
    catch (const std::exception& e)
    {
      st.SkipWithError(e.what());
      break;
    }
    benchmark::DoNotOptimize(out);
  }
  st.SetItemsProcessed((int64_t)st.iterations() * (int64_t)out.size());
}

// ---------------- Benchmarks ----------------

static void BM_Lua_Calc_Cheap(benchmark::State& st)
{
  const int N = (int)st.range(0);
  const int extra = (int)st.range(1);

  PlotDataMapRef map;
  BuildInput_Cheap(map, N, extra);

  PlotDataMapRef local;
  auto& out = local.getOrCreateNumeric("out");

  auto sn = MakeSnippetLua_Cheap(extra);
  RunCalculate_Lua(st, sn, map, out);
}

static void BM_Py_Calc_Cheap(benchmark::State& st)
{
  const int N = (int)st.range(0);
  const int extra = (int)st.range(1);

  PlotDataMapRef map;
  BuildInput_Cheap(map, N, extra);

  PlotDataMapRef local;
  auto& out = local.getOrCreateNumeric("out");

  auto sn = MakeSnippetPy_Cheap(extra);
  RunCalculate_Py(st, sn, map, out);
}

static void BM_Lua_Calc_Trig(benchmark::State& st)
{
  const int N = (int)st.range(0);
  const int extra = (int)st.range(1);

  PlotDataMapRef map;
  BuildInput_Trig(map, N, extra);

  PlotDataMapRef local;
  auto& out = local.getOrCreateNumeric("out");

  auto sn = MakeSnippetLua_Trig(extra);
  RunCalculate_Lua(st, sn, map, out);
}

static void BM_Py_Calc_Trig(benchmark::State& st)
{
  const int N = (int)st.range(0);
  const int extra = (int)st.range(1);

  PlotDataMapRef map;
  BuildInput_Trig(map, N, extra);

  PlotDataMapRef local;
  auto& out = local.getOrCreateNumeric("out");

  auto sn = MakeSnippetPy_Trig(extra);
  RunCalculate_Py(st, sn, map, out);
}

static void BM_Lua_Calc_QuatYaw(benchmark::State& st)
{
  const int N = (int)st.range(0);

  PlotDataMapRef map;
  BuildInput_Quat(map, N);

  PlotDataMapRef local;
  auto& out = local.getOrCreateNumeric("out");

  auto sn = MakeSnippetLua_QuatYaw();
  RunCalculate_Lua(st, sn, map, out);
}

static void BM_Py_Calc_QuatYaw(benchmark::State& st)
{
  const int N = (int)st.range(0);

  PlotDataMapRef map;
  BuildInput_Quat(map, N);

  PlotDataMapRef local;
  auto& out = local.getOrCreateNumeric("out");

  auto sn = MakeSnippetPy_QuatYaw();
  RunCalculate_Py(st, sn, map, out);
}

// Args: {N, extra}
BENCHMARK(BM_Lua_Calc_Cheap)
    ->Args({ 1000, 0 })
    ->Args({ 1000, 1 })
    ->Args({ 100000, 1 })
    ->Args({ 1000000, 1 });
BENCHMARK(BM_Py_Calc_Cheap)
    ->Args({ 1000, 0 })
    ->Args({ 1000, 1 })
    ->Args({ 100000, 1 })
    ->Args({ 1000000, 1 });

BENCHMARK(BM_Lua_Calc_Trig)
    ->Args({ 1000, 0 })
    ->Args({ 1000, 1 })
    ->Args({ 100000, 1 })
    ->Args({ 1000000, 1 });
BENCHMARK(BM_Py_Calc_Trig)
    ->Args({ 1000, 0 })
    ->Args({ 1000, 1 })
    ->Args({ 100000, 1 })
    ->Args({ 1000000, 1 });

// Args: {N}
BENCHMARK(BM_Lua_Calc_QuatYaw)->Arg(1000)->Arg(100000)->Arg(1000000);
BENCHMARK(BM_Py_Calc_QuatYaw)->Arg(1000)->Arg(100000)->Arg(1000000);

BENCHMARK_MAIN();
