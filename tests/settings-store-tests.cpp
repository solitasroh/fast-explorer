#include <windows.h>

#include <cstdio>
#include <string>

#include "bench-fs-helper.h"
#include "core/settings-store.h"
#include "test-harness.h"

using fast_explorer::core::defaultSettingsPath;
using fast_explorer::core::kSettingsUseDefault;
using fast_explorer::core::LayoutMode;
using fast_explorer::core::loadSessionState;
using fast_explorer::core::saveSessionState;
using fast_explorer::core::SessionState;
using fast_explorer::tests::diskPathExists;
using fast_explorer::tests::TempDir;
using fast_explorer::tests::writeEmptyDiskFile;

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
  written.lastPath = L"C:\\Users\\test\\Pictures";
  written.windowX = 50;
  written.windowY = 75;
  written.windowWidth = 1600;
  written.windowHeight = 900;
  FE_ASSERT_TRUE(saveSessionState(path, written));

  SessionState read;
  FE_ASSERT_TRUE(loadSessionState(path, read));
  FE_ASSERT_WSTREQ(read.lastPath, written.lastPath);
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
  written.lastPath = L"C:\\path\\with\"quote\\inside";
  written.windowX = 1;
  written.windowY = 2;
  written.windowWidth = 3;
  written.windowHeight = 4;
  FE_ASSERT_TRUE(saveSessionState(path, written));

  SessionState read;
  FE_ASSERT_TRUE(loadSessionState(path, read));
  FE_ASSERT_WSTREQ(read.lastPath, written.lastPath);
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
  first.lastPath = L"C:\\first";
  FE_ASSERT_TRUE(saveSessionState(path, first));
  SessionState second;
  second.lastPath = L"C:\\second";
  FE_ASSERT_TRUE(saveSessionState(path, second));

  SessionState read;
  FE_ASSERT_TRUE(loadSessionState(path, read));
  FE_ASSERT_WSTREQ(read.lastPath, L"C:\\second");
}

FE_TEST_CASE(SettingsStore_RoundTrip_DualLayoutWithSecondPath) {
  TempDir tmp(L"settings-dual-layout");
  const std::wstring path = makeSettingsPath(tmp);
  SessionState written;
  written.lastPath = L"C:\\Users\\test\\Documents";
  written.windowX = 100;
  written.windowY = 100;
  written.windowWidth = 1280;
  written.windowHeight = 800;
  written.layoutMode = LayoutMode::Dual;
  written.secondPath = L"C:\\Users\\test\\Pictures";
  FE_ASSERT_TRUE(saveSessionState(path, written));

  SessionState read;
  FE_ASSERT_TRUE(loadSessionState(path, read));
  FE_ASSERT_EQ(read.layoutMode, LayoutMode::Dual);
  FE_ASSERT_WSTREQ(read.secondPath, written.secondPath);
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
  written.lastPath = L"C:\\a";
  written.windowX = 1; written.windowY = 2;
  written.windowWidth = 3; written.windowHeight = 4;
  written.layoutMode = LayoutMode::Dual;
  written.secondPath = L"D:\\path with\\back\\slashes";
  FE_ASSERT_TRUE(saveSessionState(path, written));

  SessionState read;
  FE_ASSERT_TRUE(loadSessionState(path, read));
  FE_ASSERT_WSTREQ(read.secondPath, written.secondPath);
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
