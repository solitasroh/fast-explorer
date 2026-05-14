#include <cstdio>
#include <fcntl.h>
#include <io.h>

#include "bench/bench-cli.h"

int wmain(int argc, wchar_t* argv[]) {
  // Wide writes (fputws) on default-mode stdio silently drop or mojibake
  // non-ASCII on Windows consoles. _O_U8TEXT puts the stream into UTF-8
  // wide-char mode so paths and filenames round-trip correctly.
  _setmode(_fileno(stdout), _O_U8TEXT);
  _setmode(_fileno(stderr), _O_U8TEXT);

  const auto parsed = fast_explorer::bench::parseCommandLine(argc, argv);
  return fast_explorer::bench::runCommand(parsed, stdout, stderr);
}
