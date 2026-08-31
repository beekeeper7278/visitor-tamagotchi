# evosurvey — offline evolution reachability survey

Host-side. Compiles and drives the **shipped** `src/evolve.cpp`; nothing here
reimplements `evolve_scores()`, `evolve_pick_form()` or the EMA. A survey that
proves a copy correct proves nothing about the device, which is why the real
translation unit is linked in and only `Arduino.h` / `lvgl.h` are stubbed.

Reachability is asked as *"does an achievable care regime, driven hour by hour
through the real accumulator, land on this form?"* — not *"can these floats be
assigned"*. 24000 care regimes x 3 entry priors per decision.

    cd tools/evosurvey
    g++ -std=c++17 -O2 -o survey survey.cpp ../../src/evolve.cpp \
        -Ishim -I../../include -I../../src && ./survey

    g++ -std=c++17 -O2 -o sleepprobe sleepprobe.cpp ../../src/evolve.cpp \
        -Ishim -I../../include -I../../src && ./sleepprobe

`sleepprobe` answers one narrow question: can `care_sleep` reach the Grumpy
threshold? It cannot — see HANDOFF §8.
