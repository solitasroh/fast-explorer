#include "test-harness.h"

#include "core/location.h"

using fast_explorer::core::KnownShellLocation;
using fast_explorer::core::LocationKind;
using fast_explorer::core::displayNameForLocation;
using fast_explorer::core::parseLocation;
using fast_explorer::core::serializeLocation;

FE_TEST_CASE(Location_Parse_FileSystemPath) {
  const auto loc = parseLocation(L"C:\\Users\\me");
  FE_ASSERT_EQ(loc.kind, LocationKind::FileSystemPath);
  FE_ASSERT_WSTREQ(loc.value, L"C:\\Users\\me");
}

FE_TEST_CASE(Location_Parse_ThisPcAliases) {
  const auto a = parseLocation(L"shell:ThisPC");
  const auto b = parseLocation(L"shell:MyComputer");
  const auto c = parseLocation(L"This PC");
  const auto d = parseLocation(L" 내 PC ");
  FE_ASSERT_EQ(a.kind, LocationKind::ShellKnownFolder);
  FE_ASSERT_EQ(b.kind, LocationKind::ShellKnownFolder);
  FE_ASSERT_EQ(c.kind, LocationKind::ShellKnownFolder);
  FE_ASSERT_EQ(d.kind, LocationKind::ShellKnownFolder);
  FE_ASSERT_EQ(a.known, KnownShellLocation::ThisPC);
  FE_ASSERT_EQ(b.known, KnownShellLocation::ThisPC);
  FE_ASSERT_EQ(c.known, KnownShellLocation::ThisPC);
  FE_ASSERT_EQ(d.known, KnownShellLocation::ThisPC);
  FE_ASSERT_WSTREQ(serializeLocation(a), L"shell:ThisPC");
}

FE_TEST_CASE(Location_Parse_NetworkAliases) {
  const auto a = parseLocation(L"shell:Network");
  const auto b = parseLocation(L"shell:NetHood");
  const auto c = parseLocation(L"Network");
  const auto d = parseLocation(L"네트워크");
  const auto e = parseLocation(L"Network Shortcuts");
  FE_ASSERT_EQ(a.kind, LocationKind::ShellKnownFolder);
  FE_ASSERT_EQ(b.kind, LocationKind::ShellKnownFolder);
  FE_ASSERT_EQ(c.kind, LocationKind::ShellKnownFolder);
  FE_ASSERT_EQ(d.kind, LocationKind::ShellKnownFolder);
  FE_ASSERT_EQ(e.kind, LocationKind::ShellKnownFolder);
  FE_ASSERT_EQ(a.known, KnownShellLocation::Network);
  FE_ASSERT_EQ(b.known, KnownShellLocation::NetworkShortcuts);
  FE_ASSERT_EQ(c.known, KnownShellLocation::Network);
  FE_ASSERT_EQ(d.known, KnownShellLocation::Network);
  FE_ASSERT_EQ(e.known, KnownShellLocation::NetworkShortcuts);
  FE_ASSERT_WSTREQ(serializeLocation(a), L"shell:Network");
  FE_ASSERT_WSTREQ(serializeLocation(b), L"shell:NetHood");
}

FE_TEST_CASE(Location_Parse_GenericShellNamespace) {
  const auto apps = parseLocation(L"shell:AppsFolder");
  const auto controlPanel =
      parseLocation(L"::{26EE0668-A00A-44D7-9371-BEB064C98683}");
  const auto recycleByClsid =
      parseLocation(L"::{645FF040-5081-101B-9F08-00AA002F954E}");

  FE_ASSERT_EQ(apps.kind, LocationKind::ShellNamespace);
  FE_ASSERT_EQ(apps.known, KnownShellLocation::None);
  FE_ASSERT_WSTREQ(serializeLocation(apps), L"shell:AppsFolder");
  FE_ASSERT_EQ(controlPanel.kind, LocationKind::ShellNamespace);
  FE_ASSERT_EQ(controlPanel.known, KnownShellLocation::None);
  FE_ASSERT_WSTREQ(serializeLocation(controlPanel),
                   L"::{26EE0668-A00A-44D7-9371-BEB064C98683}");
  FE_ASSERT_EQ(recycleByClsid.kind, LocationKind::ShellNamespace);
  FE_ASSERT_EQ(recycleByClsid.known, KnownShellLocation::None);
  FE_ASSERT_WSTREQ(serializeLocation(recycleByClsid),
                   L"shell:RecycleBinFolder");
}

FE_TEST_CASE(Location_Parse_CommonShellDisplayAliases) {
  const auto recycle = parseLocation(L"휴지통");
  const auto gallery = parseLocation(L"Gallery");
  const auto home = parseLocation(L"홈");

  FE_ASSERT_EQ(recycle.kind, LocationKind::ShellNamespace);
  FE_ASSERT_WSTREQ(serializeLocation(recycle), L"shell:RecycleBinFolder");
  FE_ASSERT_EQ(gallery.kind, LocationKind::ShellNamespace);
  FE_ASSERT_WSTREQ(serializeLocation(gallery),
                   L"::{e88865ea-0e1c-4e20-9aa6-edcd0212c87c}");
  FE_ASSERT_EQ(home.kind, LocationKind::ShellNamespace);
  FE_ASSERT_WSTREQ(serializeLocation(home),
                   L"::{f874310e-b6b7-47dc-bc84-b9e6b38f5903}");
}

FE_TEST_CASE(Location_DisplayNames_AreUserFacing) {
  FE_ASSERT_WSTREQ(displayNameForLocation(parseLocation(L"shell:ThisPC")),
                   L"This PC");
  FE_ASSERT_WSTREQ(displayNameForLocation(parseLocation(L"shell:Network")),
                   L"Network");
  FE_ASSERT_WSTREQ(displayNameForLocation(parseLocation(L"C:\\Temp")),
                   L"C:\\Temp");
}
