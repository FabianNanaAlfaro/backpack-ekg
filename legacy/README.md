# Historical source snapshots

These sanitized files are kept for traceability from the old public repository [FabianNana0502/codigos](https://github.com/FabianNana0502/codigos):

- [`Mochila_ecg_legacy.cpp`](Mochila_ecg_legacy.cpp)
- [`three_lead_plot_legacy.txt`](three_leads_legacy.txt)

They are not recommended firmware. The corrected release separates acquisition from display timing, fixes the threshold ordering used by one legacy revision, constrains plotted coordinates to the TFT, avoids division by zero on an empty SD card and replaces the blocking measurement loop with explicit states.
