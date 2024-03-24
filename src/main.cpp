// OctoPrintControl - An OctoPrint Discord Bot
// Copyright (c) 2024 Taylor Talkington
// License: MIT (see LICENSE)
#include <signal.h>
#include "app.h"

static OctoPrintControl::App *app;

static void HandleSignal(int signum) {
    app->HandleSignal(signum);
}

extern "C" int main(int argc, char *argv[]) {
    app = new OctoPrintControl::App(argc, argv);

    signal(SIGINT, &HandleSignal);
    signal(SIGTERM, &HandleSignal);

    int r = app->Run();
    delete app;

    return r;
}