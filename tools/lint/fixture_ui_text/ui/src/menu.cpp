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
void log_more() {
    std::fprintf(stderr, "%s: %d\n",
                 "a continuation argument of the call", 3);  // allowed: the statement logs
}
void narrow() {
    save_message(port_text(U"Could not save"), "no save name");  // VIOLATION: the detail is beside port_text, not in it
    std::printf("%s\n", label(U"Ships lost"));                    // allowed: inside the log call's arguments
    show(txt("TEXT_X", U"Begin"), U"Unit Cap");                   // VIOLATION: the key is in txt's group, not show's
    const Row rows[] = {{"TEXT_UNIT_CAP", U"Unit Cap"}, {"id", U"Ships lost"}};  // a keyed row; VIOLATION: the other
    set(U"Page Down"); y = port_text(U"Page Down");               // VIOLATION: the first copy (by position)
    z = port_text(R"(Hello world)");                              // allowed: a raw string in the call
    w = port_text(/* see) */ U"Fleet view");                      // allowed: a ')' in a comment closes nothing
    set(U"Unit list");  // was "TEXT_FOO"                          VIOLATION: a key in a comment allows nothing
}
