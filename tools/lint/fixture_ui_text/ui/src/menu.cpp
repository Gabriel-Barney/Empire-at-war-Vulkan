// check_ui_text fixture (tools/lint/check_ui_text.py --selftest): the line numbers are part of the test.
void menu() {
    set("TEXT_BUTTON_OK", U"Okay");  // allowed: a key literal on the line
    set(U"Page Up");                 // VIOLATION: two English words
    caption = "Failed to save.";     // VIOLATION: a sentence
    label = port_text(U"V-Sync Mode");          // allowed: a port string
    std::printf("could not open the file\n");   // allowed: a log line
    id = "IDC_PORT_MODS_LIST"; path = "Data\XML\Mods.xml";  // ids and paths are not English
    // [port-text: a port-only dialog title]
    title = U"Graphics Options";     // allowed by the marker above
    // [port-text-begin: the port's quality names]
    const char* q[] = {"Medium", "Ultra"};  // allowed by the region (2)
    // [port-text: ]
    // [port-text-end]
    x = "%d";
    y = U"Unit Cap";                 // VIOLATION: after the region
}
