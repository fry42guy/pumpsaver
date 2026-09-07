#pragma once

/*
  The web UI lives in .h files, not in the .ino, deliberately.

  Arduino generates C++ forward prototypes by scanning .ino files for things
  that look like function definitions -- and it does not understand raw string
  literals. A line reading "function drawDiag(d){" inside R"HTML(...)HTML"
  gets turned into a prototype at the top of the generated .cpp and the build
  dies with "function does not name a type". Arduino does not preprocess .h
  files, so the pages are safe here however much JavaScript they grow.

  Four screens, one per file:

    page_home.h    PAGE_HOME    "/"         operator dashboard
    page_pump.h    PAGE_PUMP    "/pump"     tuning, envelope, diagnostics, sim
    page_net.h     PAGE_NET     "/network"  identity, AP, station, IP, MQTT
    page_system.h  PAGE_SYSTEM  "/system"   CAN gateway, backup, drive setup

  ui_common.h holds the design system and the nav bar as preprocessor macros,
  so each page still compiles down to a single PROGMEM string.
*/

#include "ui_common.h"
#include "page_home.h"
#include "page_pump.h"
#include "page_net.h"
#include "page_system.h"
