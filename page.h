#pragma once

/*
  The web UI lives in .h files, not in the .ino, deliberately.

  Arduino generates C++ forward prototypes by scanning .ino files for things
  that look like function definitions -- and it does not understand raw string
  literals. A line reading "function drawDiag(d){" inside R"HTML(...)HTML"
  gets turned into a prototype at the top of the generated .cpp and the build
  dies with "function does not name a type". Arduino does not preprocess .h
  files, so the pages are safe here however much JavaScript they grow.

  Three screens, one per file:

    page_easy.h   PAGE_EASY   "/"       operator landing page
    page_adv.h    PAGE_ADV    "/adv"    every parameter, plot, diagnostics
    page_wifi.h   PAGE_WIFI   "/wifi"   network and node identity

  ui_common.h holds the shared CSS and the nav bar as preprocessor macros, so
  each page still compiles down to a single PROGMEM string.
*/

#include "ui_common.h"
#include "page_easy.h"
#include "page_adv.h"
#include "page_wifi.h"
