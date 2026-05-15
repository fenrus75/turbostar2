#include <iostream>
#include <string>
#include <ncurses.h>
#include <locale.h>
#include "EventLogger.hpp"

int main(int argc, char** argv) {
    std::string log_file;
    bool debug_mode = false;
    std::string debug_string;

    // Basic argument parsing
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--log" && i + 1 < argc) {
            log_file = argv[++i];
        } else if (arg == "--debug") {
            debug_mode = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                debug_string = argv[++i];
            }
        }
    }

    auto& logger = EventLogger::getInstance();
    logger.log("Application started.");
    
    if (debug_mode) {
        logger.log("Debug mode enabled. Filter string: '" + debug_string + "'");
    }

    // Initialize ncurses
    setlocale(LC_ALL, ""); // Important for UTF-8 and ncursesw
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0); // Hide the cursor for now

    // Simple test output
    printw("Turbostar Skeleton. Press 'q' to quit.");
    refresh();

    logger.log("UI initialized.");

    int ch;
    while ((ch = getch()) != 'q') {
        logger.log("Key pressed: " + std::to_string(ch));
        
        if (debug_mode) {
            auto msg = logger.getLatestMatchingMessage(debug_string);
            if (msg) {
                // Print at the bottom status bar location
                int row, col;
                getmaxyx(stdscr, row, col);
                move(row - 1, 0);
                clrtoeol();
                std::string debug_out = ">>" + *msg + "<<";
                mvprintw(row - 1, 0, "%s", debug_out.c_str());
                // We don't want to move the actual terminal cursor, so we leave it hidden
                refresh();
            }
        }
    }

    logger.log("Exiting application loop.");

    endwin();

    if (!log_file.empty()) {
        logger.writeToFile(log_file);
    }

    return 0;
}
