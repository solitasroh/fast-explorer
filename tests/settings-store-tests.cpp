#include <windows.h>

#include <cstdio>
#include <cwchar>
#include <string>
#include <string_view>

#include "bench-fs-helper.h"
#include "core/settings-store.h"
#include "test-harness.h"

using fast_explorer::core::defaultSettingsPath;
using fast_explorer::core::kSettingsUseDefault;
using fast_explorer::core::LayoutMode;
using fast_explorer::core::LayoutOrientation;
using fast_explorer::core::LayoutPreset;
using fast_explorer::core::loadSessionState;
using fast_explorer::core::saveSessionState;
using fast_explorer::core::SessionState;
using fast_explorer::tests::diskPathExists;
using fast_explorer::tests::TempDir;

namespace {

std::wstring makeSettingsPath(const TempDir& tmp) {
  // The settings file lives directly in the temp dir; settings-store
  // creates the parent dir on demand so we pass a nested path here too
  // to exercise that branch.
  std::wstring out(tmp.path());
  out.append(L"\\nested\\settings.json");
  return out;
}

bool writeRawUtf8(const std::wstring& path, const char* body) {
  HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                         CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  const DWORD len = static_cast<DWORD>(std::strlen(body));
  const BOOL ok = WriteFile(h, body, len, &written, nullptr);
  CloseHandle(h);
  return ok && written == len;
}

std::wstring uniqueTempPath(std::wstring_view tag) {
  wchar_t buf[MAX_PATH];
  GetTempPathW(MAX_PATH, buf);
  std::wstring p = buf;
  p += L"\\";
  p += tag;
  wchar_t tick[32];
  swprintf_s(tick, L"_%llu", (unsigned long long)GetTickCount64());
  p += tick;
  p += L".json";
  return p;
}

void writeRawBytes(const std::wstring& path, std::string_view bytes) {
  HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                         CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return;
  DWORD wrote = 0;
  WriteFile(h, bytes.data(), static_cast<DWORD>(bytes.size()), &wrote, nullptr);
  CloseHandle(h);
}

}  // namespace

FE_TEST_CASE(SettingsStore_DefaultState_HasSentinels) {
  SessionState s;
  FE_ASSERT_TRUE(s.lastPath.empty());
  FE_ASSERT_EQ(s.windowX, kSettingsUseDefault);
  FE_ASSERT_EQ(s.windowY, kSettingsUseDefault);
  FE_ASSERT_EQ(s.windowWidth, kSettingsUseDefault);
  FE_ASSERT_EQ(s.windowHeight, kSettingsUseDefault);
  FE_ASSERT_EQ(s.layoutMode, LayoutMode::Single);
  FE_ASSERT_TRUE(s.secondPath.empty());
  FE_ASSERT_EQ(s.orientation, LayoutOrientation::Vertical);
}

FE_TEST_CASE(SettingsStore_Load_NonexistentFile_ReturnsFalseAndKeepsDefaults) {
  TempDir tmp(L"settings-missing");
  const std::wstring path = makeSettingsPath(tmp);
  SessionState s;
  s.lastPath = L"will-be-overwritten";
  s.windowX = 42;
  FE_ASSERT_FALSE(loadSessionState(path, s));
  FE_ASSERT_TRUE(s.lastPath.empty());
  FE_ASSERT_EQ(s.windowX, kSettingsUseDefault);
}

FE_TEST_CASE(SettingsStore_Save_CreatesParentDirAndFile) {
  TempDir tmp(L"settings-save-mkdir");
  const std::wstring path = makeSettingsPath(tmp);
  SessionState s;
  s.lastPath = L"C:\\Users\\test\\Documents";
  s.windowX = 100;
  s.windowY = 200;
  s.windowWidth = 1280;
  s.windowHeight = 800;
  FE_ASSERT_TRUE(saveSessionState(path, s));
  FE_ASSERT_TRUE(diskPathExists(path));
}

FE_TEST_CASE(SettingsStore_RoundTrip_PopulatedState) {
  TempDir tmp(L"settings-roundtrip");
  const std::wstring path = makeSettingsPath(tmp);
  SessionState written;
  written.panes[0].tabs.push_back({L"C:\\Users\\test\\Pictures"});
  written.panes[0].activeTab = 0;
  written.windowX = 50;
  written.windowY = 75;
  written.windowWidth = 1600;
  written.windowHeight = 900;
  FE_ASSERT_TRUE(saveSessionState(path, written));

  SessionState read;
  FE_ASSERT_TRUE(loadSessionState(path, read));
  FE_ASSERT_EQ(read.panes[0].tabs.size(), static_cast<std::size_t>(1));
  FE_ASSERT_WSTREQ(read.panes[0].tabs[0].path, written.panes[0].tabs[0].path);
  FE_ASSERT_EQ(read.windowX, written.windowX);
  FE_ASSERT_EQ(read.windowY, written.windowY);
  FE_ASSERT_EQ(read.windowWidth, written.windowWidth);
  FE_ASSERT_EQ(read.windowHeight, written.windowHeight);
}

FE_TEST_CASE(SettingsStore_RoundTrip_EmptyPathPreserved) {
  TempDir tmp(L"settings-empty-path");
  const std::wstring path = makeSettingsPath(tmp);
  SessionState written;
  written.windowX = 0;
  written.windowY = 0;
  written.windowWidth = 320;
  written.windowHeight = 240;
  FE_ASSERT_TRUE(saveSessionState(path, written));

  SessionState read;
  FE_ASSERT_TRUE(loadSessionState(path, read));
  FE_ASSERT_TRUE(read.lastPath.empty());
  FE_ASSERT_EQ(read.windowX, 0);
}

FE_TEST_CASE(SettingsStore_RoundTrip_PathWithBackslashAndQuote) {
  TempDir tmp(L"settings-escape");
  const std::wstring path = makeSettingsPath(tmp);
  SessionState written;
  written.panes[0].tabs.push_back({L"C:\\path\\with\"quote\\inside"});
  written.panes[0].activeTab = 0;
  written.windowX = 1;
  written.windowY = 2;
  written.windowWidth = 3;
  written.windowHeight = 4;
  FE_ASSERT_TRUE(saveSessionState(path, written));

  SessionState read;
  FE_ASSERT_TRUE(loadSessionState(path, read));
  FE_ASSERT_EQ(read.panes[0].tabs.size(), static_cast<std::size_t>(1));
  FE_ASSERT_WSTREQ(read.panes[0].tabs[0].path, written.panes[0].tabs[0].path);
}

FE_TEST_CASE(SettingsStore_Load_MalformedJson_ReturnsFalse) {
  TempDir tmp(L"settings-malformed");
  const std::wstring path = makeSettingsPath(tmp);
  // settings-store does not auto-create the dir on load, so seed it
  // by writing the malformed body directly into the nested folder
  // after a successful save call (which creates the parent).
  SessionState seed;
  FE_ASSERT_TRUE(saveSessionState(path, seed));
  FE_ASSERT_TRUE(writeRawUtf8(path, "{\"last_path\": \"unterminated"));

  SessionState s;
  FE_ASSERT_FALSE(loadSessionState(path, s));
  FE_ASSERT_TRUE(s.lastPath.empty());
}

FE_TEST_CASE(SettingsStore_Load_UnknownKey_StillSucceeds) {
  TempDir tmp(L"settings-forward-compat");
  const std::wstring path = makeSettingsPath(tmp);
  SessionState seed;
  FE_ASSERT_TRUE(saveSessionState(path, seed));
  // Forward-compatibility: a future field should not break the
  // current parser.
  FE_ASSERT_TRUE(writeRawUtf8(
      path,
      "{\"last_path\":\"C:\\\\a\",\"window_x\":10,\"future_field\":42,"
      "\"window_y\":20,\"window_w\":300,\"window_h\":200}"));

  SessionState s;
  FE_ASSERT_TRUE(loadSessionState(path, s));
  FE_ASSERT_WSTREQ(s.lastPath, L"C:\\a");
  FE_ASSERT_EQ(s.windowX, 10);
  FE_ASSERT_EQ(s.windowY, 20);
}

FE_TEST_CASE(SettingsStore_Save_OverwriteExistingFile) {
  TempDir tmp(L"settings-overwrite");
  const std::wstring path = makeSettingsPath(tmp);
  SessionState first;
  first.panes[0].tabs.push_back({L"C:\\first"});
  first.panes[0].activeTab = 0;
  FE_ASSERT_TRUE(saveSessionState(path, first));
  SessionState second;
  second.panes[0].tabs.push_back({L"C:\\second"});
  second.panes[0].activeTab = 0;
  FE_ASSERT_TRUE(saveSessionState(path, second));

  SessionState read;
  FE_ASSERT_TRUE(loadSessionState(path, read));
  FE_ASSERT_EQ(read.panes[0].tabs.size(), static_cast<std::size_t>(1));
  FE_ASSERT_WSTREQ(read.panes[0].tabs[0].path, L"C:\\second");
}

FE_TEST_CASE(SettingsStore_RoundTrip_DualLayoutWithSecondPath) {
  TempDir tmp(L"settings-dual-layout");
  const std::wstring path = makeSettingsPath(tmp);
  SessionState written;
  written.panes[0].tabs.push_back({L"C:\\Users\\test\\Documents"});
  written.panes[0].activeTab = 0;
  written.panes[1].tabs.push_back({L"C:\\Users\\test\\Pictures"});
  written.panes[1].activeTab = 0;
  written.paneCount = 2;
  written.preset = LayoutPreset::Dual_V;
  written.windowX = 100;
  written.windowY = 100;
  written.windowWidth = 1280;
  written.windowHeight = 800;
  FE_ASSERT_TRUE(saveSessionState(path, written));

  SessionState read;
  FE_ASSERT_TRUE(loadSessionState(path, read));
  FE_ASSERT_EQ(read.paneCount, std::size_t{2});
  FE_ASSERT_EQ(read.preset, LayoutPreset::Dual_V);
  FE_ASSERT_EQ(read.panes[1].tabs.size(), static_cast<std::size_t>(1));
  FE_ASSERT_WSTREQ(read.panes[1].tabs[0].path, written.panes[1].tabs[0].path);
}

FE_TEST_CASE(SettingsStore_RoundTrip_SingleLayoutEmptySecondPath) {
  TempDir tmp(L"settings-single-empty-second");
  const std::wstring path = makeSettingsPath(tmp);
  SessionState written;
  written.lastPath = L"C:\\";
  written.windowX = 0;
  written.windowY = 0;
  written.windowWidth = 320;
  written.windowHeight = 240;
  // layoutMode and secondPath at defaults.
  FE_ASSERT_TRUE(saveSessionState(path, written));

  SessionState read;
  FE_ASSERT_TRUE(loadSessionState(path, read));
  FE_ASSERT_EQ(read.layoutMode, LayoutMode::Single);
  FE_ASSERT_TRUE(read.secondPath.empty());
}

FE_TEST_CASE(SettingsStore_Load_V1FileMissingNewKeys_DefaultsApplied) {
  // A settings file written by a pre-v2 build does not carry
  // layout_mode or second_path. The reader must accept it and fall
  // back to Single + empty so existing users get session restore.
  TempDir tmp(L"settings-v1-compat");
  const std::wstring path = makeSettingsPath(tmp);
  SessionState seed;
  FE_ASSERT_TRUE(saveSessionState(path, seed));
  FE_ASSERT_TRUE(writeRawUtf8(
      path,
      "{\"last_path\":\"C:\\\\v1\",\"window_x\":10,\"window_y\":20,"
      "\"window_w\":640,\"window_h\":480}"));

  SessionState s;
  FE_ASSERT_TRUE(loadSessionState(path, s));
  FE_ASSERT_WSTREQ(s.lastPath, L"C:\\v1");
  FE_ASSERT_EQ(s.windowX, 10);
  FE_ASSERT_EQ(s.layoutMode, LayoutMode::Single);
  FE_ASSERT_TRUE(s.secondPath.empty());
}

FE_TEST_CASE(SettingsStore_Load_UnknownLayoutModeString_FallsBackToSingle) {
  // Forward compatibility: a future "tri" mode written by a newer
  // build should load lossily rather than discard the entire file.
  TempDir tmp(L"settings-unknown-layout");
  const std::wstring path = makeSettingsPath(tmp);
  SessionState seed;
  FE_ASSERT_TRUE(saveSessionState(path, seed));
  FE_ASSERT_TRUE(writeRawUtf8(
      path,
      "{\"last_path\":\"\",\"window_x\":0,\"window_y\":0,"
      "\"window_w\":0,\"window_h\":0,\"layout_mode\":\"tri\","
      "\"second_path\":\"C:\\\\b\"}"));

  SessionState s;
  FE_ASSERT_TRUE(loadSessionState(path, s));
  FE_ASSERT_EQ(s.layoutMode, LayoutMode::Single);
  FE_ASSERT_WSTREQ(s.secondPath, L"C:\\b");
}

FE_TEST_CASE(SettingsStore_Load_WindowFieldWrongType_ReturnsFalse) {
  // Symmetric to the layout_mode wrong-type case below: an integer
  // key receiving a string should fail the whole load (strict typing
  // distinguishes schema corruption from forward-compat unknowns).
  TempDir tmp(L"settings-window-wrong-type");
  const std::wstring path = makeSettingsPath(tmp);
  SessionState seed;
  FE_ASSERT_TRUE(saveSessionState(path, seed));
  FE_ASSERT_TRUE(writeRawUtf8(
      path,
      "{\"last_path\":\"\",\"window_x\":\"oops\"}"));

  SessionState s;
  FE_ASSERT_FALSE(loadSessionState(path, s));
}

FE_TEST_CASE(SettingsStore_Load_LayoutModeWrongType_ReturnsFalse) {
  // Strict on type errors (integer where string is expected) — same
  // policy as last_path. Distinguishes "future field we tolerate" from
  // "schema corruption we refuse".
  TempDir tmp(L"settings-layout-wrong-type");
  const std::wstring path = makeSettingsPath(tmp);
  SessionState seed;
  FE_ASSERT_TRUE(saveSessionState(path, seed));
  FE_ASSERT_TRUE(writeRawUtf8(
      path,
      "{\"last_path\":\"\",\"layout_mode\":1}"));

  SessionState s;
  FE_ASSERT_FALSE(loadSessionState(path, s));
}

FE_TEST_CASE(SettingsStore_RoundTrip_SecondPathBackslashEscape) {
  TempDir tmp(L"settings-second-path-escape");
  const std::wstring path = makeSettingsPath(tmp);
  SessionState written;
  written.panes[0].tabs.push_back({L"C:\\a"});
  written.panes[0].activeTab = 0;
  written.panes[1].tabs.push_back({L"D:\\path with\\back\\slashes"});
  written.panes[1].activeTab = 0;
  written.paneCount = 2;
  written.preset = LayoutPreset::Dual_V;
  written.windowX = 1; written.windowY = 2;
  written.windowWidth = 3; written.windowHeight = 4;
  FE_ASSERT_TRUE(saveSessionState(path, written));

  SessionState read;
  FE_ASSERT_TRUE(loadSessionState(path, read));
  FE_ASSERT_EQ(read.panes[1].tabs.size(), static_cast<std::size_t>(1));
  FE_ASSERT_WSTREQ(read.panes[1].tabs[0].path, written.panes[1].tabs[0].path);
}

FE_TEST_CASE(SettingsStore_RoundTrip_HorizontalOrientation) {
  TempDir tmp(L"settings-orient-horizontal");
  const std::wstring path = makeSettingsPath(tmp);
  SessionState written;
  written.panes[0].tabs.push_back({L"C:\\"});
  written.panes[0].activeTab = 0;
  written.panes[1].tabs.push_back({L"D:\\"});
  written.panes[1].activeTab = 0;
  written.paneCount = 2;
  written.preset = LayoutPreset::Dual_H;
  written.windowX = 0; written.windowY = 0;
  written.windowWidth = 1280; written.windowHeight = 800;
  FE_ASSERT_TRUE(saveSessionState(path, written));

  SessionState read;
  FE_ASSERT_TRUE(loadSessionState(path, read));
  FE_ASSERT_EQ(read.preset, LayoutPreset::Dual_H);
}

FE_TEST_CASE(SettingsStore_RoundTrip_VerticalOrientationDefault) {
  TempDir tmp(L"settings-orient-vertical-default");
  const std::wstring path = makeSettingsPath(tmp);
  SessionState written;
  written.lastPath = L"C:\\";
  written.windowX = 0; written.windowY = 0;
  written.windowWidth = 320; written.windowHeight = 240;
  // orientation field left at default.
  FE_ASSERT_TRUE(saveSessionState(path, written));

  SessionState read;
  FE_ASSERT_TRUE(loadSessionState(path, read));
  FE_ASSERT_EQ(read.orientation, LayoutOrientation::Vertical);
}

FE_TEST_CASE(SettingsStore_Load_V2FileMissingOrientation_DefaultsVertical) {
  // A settings file written by a pre-v3 build does not carry the
  // orientation key. Loader must fall back to Vertical so the user
  // sees the same dual layout that prior versions of this app
  // shipped exclusively.
  TempDir tmp(L"settings-v2-compat");
  const std::wstring path = makeSettingsPath(tmp);
  SessionState seed;
  FE_ASSERT_TRUE(saveSessionState(path, seed));
  FE_ASSERT_TRUE(writeRawUtf8(
      path,
      "{\"last_path\":\"C:\\\\v2\",\"window_x\":10,\"window_y\":20,"
      "\"window_w\":640,\"window_h\":480,\"layout_mode\":\"dual\","
      "\"second_path\":\"D:\\\\v2\"}"));

  SessionState s;
  FE_ASSERT_TRUE(loadSessionState(path, s));
  FE_ASSERT_EQ(s.layoutMode, LayoutMode::Dual);
  FE_ASSERT_WSTREQ(s.secondPath, L"D:\\v2");
  FE_ASSERT_EQ(s.orientation, LayoutOrientation::Vertical);
}

FE_TEST_CASE(SettingsStore_Load_UnknownOrientationString_FallsBackToVertical) {
  TempDir tmp(L"settings-unknown-orient");
  const std::wstring path = makeSettingsPath(tmp);
  SessionState seed;
  FE_ASSERT_TRUE(saveSessionState(path, seed));
  FE_ASSERT_TRUE(writeRawUtf8(
      path,
      "{\"last_path\":\"\",\"window_x\":0,\"window_y\":0,"
      "\"window_w\":0,\"window_h\":0,\"layout_mode\":\"single\","
      "\"second_path\":\"\",\"orientation\":\"diagonal\"}"));

  SessionState s;
  FE_ASSERT_TRUE(loadSessionState(path, s));
  FE_ASSERT_EQ(s.orientation, LayoutOrientation::Vertical);
}

FE_TEST_CASE(SettingsStore_Load_OrientationWrongType_ReturnsFalse) {
  TempDir tmp(L"settings-orient-wrong-type");
  const std::wstring path = makeSettingsPath(tmp);
  SessionState seed;
  FE_ASSERT_TRUE(saveSessionState(path, seed));
  FE_ASSERT_TRUE(writeRawUtf8(
      path,
      "{\"last_path\":\"\",\"orientation\":0}"));

  SessionState s;
  FE_ASSERT_FALSE(loadSessionState(path, s));
}

FE_TEST_CASE(SettingsStore_DefaultSettingsPath_NonEmptyOnTypicalEnv) {
  // Outside the portable override + when LocalAppData is available
  // (true on CI Windows runners + typical developer machines), the
  // resolver should return a non-empty path ending in settings.json.
  const std::wstring p = defaultSettingsPath();
  if (!p.empty()) {
    FE_ASSERT_TRUE(p.size() > std::wstring(L"settings.json").size());
    const std::wstring tail(p.end() - 13, p.end());
    FE_ASSERT_WSTREQ(tail, L"settings.json");
  }
}

// T9 (v0.2 schema v4) — view toggles persistence + missing-key
// migration behaviour. A v3 file (no view_show_* keys) loads with the
// defaults baked into SessionState (showHidden=false, showExtensions=true).
FE_TEST_CASE(SettingsStore_RoundTrip_ViewToggles_Persisted) {
  TempDir tmp(L"settings-view-toggles-roundtrip");
  const std::wstring path = makeSettingsPath(tmp);
  SessionState out;
  out.showHidden = true;
  out.showExtensions = false;
  FE_ASSERT_TRUE(saveSessionState(path, out));

  SessionState back;
  FE_ASSERT_TRUE(loadSessionState(path, back));
  FE_ASSERT_TRUE(back.showHidden);
  FE_ASSERT_FALSE(back.showExtensions);
}

FE_TEST_CASE(SettingsStore_Load_V3FileWithoutViewKeys_UsesV4Defaults) {
  TempDir tmp(L"settings-v3-to-v4-migration");
  const std::wstring path = makeSettingsPath(tmp);
  // saveSessionState creates the (nested) parent dir on demand;
  // writeRawUtf8 is a raw CreateFileW and does not, so seed first.
  SessionState seed;
  FE_ASSERT_TRUE(saveSessionState(path, seed));
  // v3 representation — only the keys that existed before v0.2; no
  // view_show_hidden / view_show_extensions. saveSessionState always
  // writes the v4 keys, so to simulate v3 we hand-build the JSON.
  FE_ASSERT_TRUE(writeRawUtf8(
      path,
      "{\"last_path\":\"C:\\\\demo\","
      "\"window_x\":100,\"window_y\":100,"
      "\"window_w\":800,\"window_h\":600,"
      "\"layout_mode\":\"single\",\"second_path\":\"\","
      "\"orientation\":\"vertical\"}"));

  SessionState s;
  FE_ASSERT_TRUE(loadSessionState(path, s));
  // SessionState defaults survive missing-key loads (forward-compat).
  FE_ASSERT_FALSE(s.showHidden);
  FE_ASSERT_TRUE(s.showExtensions);
  // And the rest of the v3 payload is intact.
  FE_ASSERT_EQ(s.windowX, 100);
}

FE_TEST_CASE(SettingsStore_v5_RoundTrip_QuadA) {
  SessionState in{};
  in.windowX = 100; in.windowY = 50; in.windowWidth = 1280; in.windowHeight = 800;
  in.panes[0].tabs.push_back({L"C:/a"}); in.panes[0].activeTab = 0;
  in.panes[1].tabs.push_back({L"D:/b"}); in.panes[1].activeTab = 0;
  in.panes[2].tabs.push_back({L"E:/c"}); in.panes[2].activeTab = 0;
  in.panes[3].tabs.push_back({L"F:/d"}); in.panes[3].activeTab = 0;
  in.paneCount = 4;
  in.activePane = 1;
  in.preset = LayoutPreset::Quad_A;
  in.ratiosPerPreset[size_t(LayoutPreset::Quad_A)] = {{0.6f, 0.5f, 0.5f}};

  const std::wstring path = uniqueTempPath(L"fe_v5_qa");
  FE_ASSERT_TRUE(saveSessionState(path, in));

  SessionState out{};
  FE_ASSERT_TRUE(loadSessionState(path, out));
  FE_ASSERT_EQ(out.paneCount, std::size_t{4});
  FE_ASSERT_EQ(out.activePane, std::size_t{1});
  FE_ASSERT_EQ(out.preset, LayoutPreset::Quad_A);
  FE_ASSERT_EQ(out.panes[0].tabs.size(), static_cast<std::size_t>(1));
  FE_ASSERT_WSTREQ(out.panes[0].tabs[0].path, L"C:/a");
  FE_ASSERT_WSTREQ(out.panes[1].tabs[0].path, L"D:/b");
  FE_ASSERT_WSTREQ(out.panes[2].tabs[0].path, L"E:/c");
  FE_ASSERT_WSTREQ(out.panes[3].tabs[0].path, L"F:/d");
  const auto& r = out.ratiosPerPreset[size_t(LayoutPreset::Quad_A)];
  FE_ASSERT_TRUE(r.ratios[0] > 0.59f && r.ratios[0] < 0.61f);
  DeleteFileW(path.c_str());
}

FE_TEST_CASE(SettingsStore_v4_to_v5_DualV_Migrates) {
  const std::string v4Json =
    "{\n"
    "  \"last_path\": \"C:/old\",\n"
    "  \"window_x\": 1, \"window_y\": 2, \"window_w\": 800, \"window_h\": 600,\n"
    "  \"layout_mode\": \"dual\",\n"
    "  \"second_path\": \"D:/old\",\n"
    "  \"orientation\": \"vertical\",\n"
    "  \"view_show_hidden\": 0,\n"
    "  \"view_show_extensions\": 1\n"
    "}\n";
  const std::wstring path = uniqueTempPath(L"fe_v4_dv");
  writeRawBytes(path, v4Json);

  SessionState out{};
  FE_ASSERT_TRUE(loadSessionState(path, out));
  FE_ASSERT_EQ(out.paneCount, std::size_t{2});
  FE_ASSERT_EQ(out.preset, LayoutPreset::Dual_V);
  // v4->v5->v6 migration: last_path/second_path land in panes[i].tabs[0].path
  FE_ASSERT_EQ(out.panes[0].tabs.size(), static_cast<std::size_t>(1));
  FE_ASSERT_WSTREQ(out.panes[0].tabs[0].path, L"C:/old");
  FE_ASSERT_EQ(out.panes[1].tabs.size(), static_cast<std::size_t>(1));
  FE_ASSERT_WSTREQ(out.panes[1].tabs[0].path, L"D:/old");
  FE_ASSERT_WSTREQ(out.lastPath,   L"C:/old");
  FE_ASSERT_WSTREQ(out.secondPath, L"D:/old");
  DeleteFileW(path.c_str());
}

FE_TEST_CASE(SettingsStore_v4_to_v5_Single_Migrates) {
  const std::string v4Json =
    "{\n"
    "  \"last_path\": \"X:/proj\",\n"
    "  \"layout_mode\": \"single\"\n"
    "}\n";
  const std::wstring path = uniqueTempPath(L"fe_v4_s");
  writeRawBytes(path, v4Json);
  SessionState out{};
  FE_ASSERT_TRUE(loadSessionState(path, out));
  FE_ASSERT_EQ(out.paneCount, std::size_t{1});
  FE_ASSERT_EQ(out.preset, LayoutPreset::Single);
  FE_ASSERT_EQ(out.panes[0].tabs.size(), static_cast<std::size_t>(1));
  FE_ASSERT_WSTREQ(out.panes[0].tabs[0].path, L"X:/proj");
  DeleteFileW(path.c_str());
}

FE_TEST_CASE(SettingsStore_v4_to_v5_DualH_Migrates) {
  const std::string v4Json =
    "{\n"
    "  \"last_path\": \"A:/\",\n"
    "  \"second_path\": \"B:/\",\n"
    "  \"layout_mode\": \"dual\",\n"
    "  \"orientation\": \"horizontal\"\n"
    "}\n";
  const std::wstring path = uniqueTempPath(L"fe_v4_dh");
  writeRawBytes(path, v4Json);
  SessionState out{};
  FE_ASSERT_TRUE(loadSessionState(path, out));
  FE_ASSERT_EQ(out.preset, LayoutPreset::Dual_H);
  DeleteFileW(path.c_str());
}

FE_TEST_CASE(SettingsStore_V5File_MigratesToV6Tabs) {
  TempDir tmp(L"settings-v5-migrate");
  const std::wstring path = makeSettingsPath(tmp);
  // Raw v5 file: pane_paths[2] with paneCount=2.
  // Include layout_mode:"dual" and orientation:"vertical" so the v4->v5
  // migration block (which fires for schema_version < 6) sets paneCount=2
  // and Dual_V preset rather than falling back to Single/paneCount=1.
  const char* v5Body =
    "{\"schema_version\":5,"
    "\"pane_count\":2,\"active_pane\":0,"
    "\"layout_mode\":\"dual\",\"orientation\":\"vertical\","
    "\"pane_paths\":[\"C:\\\\one\",\"D:\\\\two\"],"
    "\"preset\":\"dual_v\"}";
  // Seed a save to create the parent directory hierarchy, then overwrite
  // with the exact raw v5 JSON we want to test migration from.
  { SessionState seed; FE_ASSERT_TRUE(saveSessionState(path, seed)); }
  writeRawUtf8(path, v5Body);

  SessionState s;
  FE_ASSERT_TRUE(loadSessionState(path, s));
  FE_ASSERT_EQ(s.paneCount, static_cast<std::size_t>(2));
  FE_ASSERT_EQ(s.panes[0].tabs.size(), static_cast<std::size_t>(1));
  FE_ASSERT_WSTREQ(s.panes[0].tabs[0].path, L"C:\\one");
  FE_ASSERT_EQ(s.panes[0].activeTab, static_cast<std::size_t>(0));
  FE_ASSERT_EQ(s.panes[1].tabs.size(), static_cast<std::size_t>(1));
  FE_ASSERT_WSTREQ(s.panes[1].tabs[0].path, L"D:\\two");
}

FE_TEST_CASE(SettingsStore_V6_RoundTripPreservesTabs) {
  TempDir tmp(L"settings-v6-rt");
  const std::wstring path = makeSettingsPath(tmp);
  SessionState s;
  s.paneCount = 2;
  s.activePane = 1;
  s.preset = LayoutPreset::Dual_V;
  s.panes[0].tabs.push_back({L"C:\\Users\\me\\Docs"});
  s.panes[0].tabs.push_back({L"D:\\proj"});
  s.panes[0].activeTab = 1;
  s.panes[1].tabs.push_back({L"C:\\Users\\me"});
  s.panes[1].activeTab = 0;
  FE_ASSERT_TRUE(saveSessionState(path, s));

  SessionState loaded;
  FE_ASSERT_TRUE(loadSessionState(path, loaded));
  FE_ASSERT_EQ(loaded.panes[0].tabs.size(), static_cast<std::size_t>(2));
  FE_ASSERT_WSTREQ(loaded.panes[0].tabs[0].path, L"C:\\Users\\me\\Docs");
  FE_ASSERT_WSTREQ(loaded.panes[0].tabs[1].path, L"D:\\proj");
  FE_ASSERT_EQ(loaded.panes[0].activeTab, static_cast<std::size_t>(1));
  FE_ASSERT_EQ(loaded.panes[1].tabs.size(), static_cast<std::size_t>(1));
}

FE_TEST_CASE(SettingsStore_V6_EmptyTabsArrayBecomesHomePlaceholder) {
  TempDir tmp(L"settings-v6-empty-tabs");
  const std::wstring path = makeSettingsPath(tmp);
  { SessionState seed; FE_ASSERT_TRUE(saveSessionState(path, seed)); }
  const char* body =
    "{\"schema_version\":6,\"pane_count\":1,\"active_pane\":0,"
    "\"panes\":[{\"tabs\":[],\"active_tab\":0}],"
    "\"preset\":\"single\"}";
  writeRawUtf8(path, body);

  SessionState s;
  FE_ASSERT_TRUE(loadSessionState(path, s));
  FE_ASSERT_EQ(s.panes[0].tabs.size(), static_cast<std::size_t>(1));
  FE_ASSERT_TRUE(s.panes[0].tabs[0].path.empty());  // Home placeholder
}

FE_TEST_CASE(SettingsStore_V6_ClampActiveTabBeyondRange) {
  TempDir tmp(L"settings-v6-clamp");
  const std::wstring path = makeSettingsPath(tmp);
  { SessionState seed; FE_ASSERT_TRUE(saveSessionState(path, seed)); }
  const char* body =
    "{\"schema_version\":6,\"pane_count\":1,\"active_pane\":0,"
    "\"panes\":[{\"tabs\":[{\"path\":\"C:\\\\a\"}],\"active_tab\":99}],"
    "\"preset\":\"single\"}";
  writeRawUtf8(path, body);

  SessionState s;
  FE_ASSERT_TRUE(loadSessionState(path, s));
  FE_ASSERT_EQ(s.panes[0].activeTab, static_cast<std::size_t>(0));
}
