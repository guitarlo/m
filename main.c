#include <complex.h>
#include <ncurses.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_APPS 512
#define MAX_SEARCH 64

// Struktur für App-Informationen
typedef struct {
    char *display_name;  // Anzeigename (ohne .desktop)
    char *file_name;     // Dateiname (mit .desktop)
    int is_flatpak;      // 1 = Flatpak, 0 = normale App
} App;

// Entfernt die .desktop-Endung vom Namen
void clean_name(char *name) {
    char *ext = strstr(name, ".desktop");
    if (ext) *ext = '\0';
}

// Vergleichsfunktion für qsort (alphabetische Sortierung)
int compare_apps(const void *a, const void *b) {
    App *app_a = (App *)a;
    App *app_b = (App *)b;
    return strcasecmp(app_a->display_name, app_b->display_name);
}

// Prüft, ob der Suchbegriff im App-Namen vorkommt (case-insensitive)
int matches_search(const char *app_name, const char *search) {
    if (strlen(search) == 0) return 1;

    char app_lower[256], search_lower[MAX_SEARCH];

    for (int i = 0; app_name[i] && i < 255; i++) {
        app_lower[i] = tolower(app_name[i]);
        app_lower[i + 1] = '\0';
    }

    for (int i = 0; search[i] && i < MAX_SEARCH - 1; i++) {
        search_lower[i] = tolower(search[i]);
        search_lower[i + 1] = '\0';
    }

    return strstr(app_lower, search_lower) != NULL;
}

// Filtert Apps basierend auf dem Suchbegriff
int filter_apps(App *apps, int total_apps, int *filtered_indices, const char *search) {
    int count = 0;
    for (int i = 0; i < total_apps; i++) {
        if (matches_search(apps[i].display_name, search)) {
            filtered_indices[count++] = i;
        }
    }
    return count;
}

// Lädt Apps aus einem Verzeichnis
int load_apps_from_dir(const char *path, App *apps, int start_index, int max_apps, int is_flatpak) {
    DIR *dir;
    struct dirent *entry;
    int count = 0;

    dir = opendir(path);
    if (!dir) {
        return 0;  // Verzeichnis existiert nicht oder kann nicht geöffnet werden
    }

    while ((entry = readdir(dir)) != NULL && (start_index + count) < max_apps) {
        if (strstr(entry->d_name, ".desktop")) {
            apps[start_index + count].file_name = strdup(entry->d_name);
            apps[start_index + count].display_name = strdup(entry->d_name);
            clean_name(apps[start_index + count].display_name);
            apps[start_index + count].is_flatpak = is_flatpak;
            count++;
        }
    }
    closedir(dir);

    return count;
}

int main() {
    App apps[MAX_APPS];
    int app_count = 0;

    // Lade normale Apps aus /usr/share/applications
    app_count += load_apps_from_dir("/usr/share/applications", apps, app_count, MAX_APPS, 0);

    // Lade Flatpak-Apps aus ~/.local/share/flatpak/exports/share/applications
    char flatpak_user_path[512];
    snprintf(flatpak_user_path, sizeof(flatpak_user_path),
            "%s/.local/share/flatpak/exports/share/applications", getenv("HOME"));
    app_count += load_apps_from_dir(flatpak_user_path, apps, app_count, MAX_APPS, 1);

    // Lade system-weite Flatpak-Apps aus /var/lib/flatpak/exports/share/applications
    app_count += load_apps_from_dir("/var/lib/flatpak/exports/share/applications",
            apps, app_count, MAX_APPS, 1);

    if (app_count == 0) {
        fprintf(stderr, "Fehler: Keine Apps gefunden\n");
        return 1;
    }

    // Sortiere Apps alphabetisch
    qsort(apps, app_count, sizeof(App), compare_apps);

    // Initialisiere ncurses
    initscr();
    noecho();
    curs_set(0);
    keypad(stdscr, TRUE);

    // Variablen für die Benutzeroberfläche
    int highlight = 0;
    int choice = -1;
    int offset = 0;
    char search[MAX_SEARCH] = "";
    int search_len = 0;

    // Gefilterte App-Indizes
    int filtered_indices[MAX_APPS];
    int filtered_count = app_count;
    for (int i = 0; i < app_count; i++) {
        filtered_indices[i] = i;
    }

    // Fenster-Konfiguration
    int win_height = 22;
    if (win_height > LINES - 2) win_height = LINES - 2;
    int win_width = 60;
    int start_y = (LINES - win_height) / 2;
    int start_x = (COLS - win_width) / 2;

    WINDOW *menu_win = newwin(win_height, win_width, start_y, start_x);
    keypad(menu_win, TRUE);

    int max_visible = win_height - 4;

    // Hauptschleife
    while (1) {
        werase(menu_win);
        box(menu_win, 0, 0);

        // Titel mit App-Anzahl
       mvwprintw(menu_win, 0, 2, " m (%d/%d) by C. Mansfeld ", filtered_count, app_count);

        // Suchleiste anzeigen
        mvwprintw(menu_win, 1, 2, "Suche: %s", search);
        if (search_len > 0) {
            wattron(menu_win, A_BLINK);
            waddch(menu_win, '_');
            wattroff(menu_win, A_BLINK);
        }
        mvwhline(menu_win, 2, 1, ACS_HLINE, win_width - 2);

        // Scroll-Logik anpassen
        if (highlight < offset) {
            offset = highlight;
        }
        if (highlight >= offset + max_visible) {
            offset = highlight - max_visible + 1;
        }

        // Apps anzeigen
        for (int i = 0; i < max_visible; i++) {
            int list_index = i + offset;
            if (list_index < filtered_count) {
                int app_index = filtered_indices[list_index];

                if (list_index == highlight) {
                    wattron(menu_win, A_REVERSE);
                }

                // Optional: Flatpak-Indikator anzeigen
                char display_text[64];
                if (apps[app_index].is_flatpak) {
                    snprintf(display_text, sizeof(display_text), "[F] %-52s",
                            apps[app_index].display_name);
                } else {
                    snprintf(display_text, sizeof(display_text), "    %-52s",
                            apps[app_index].display_name);
                }

                mvwprintw(menu_win, i + 3, 2, "%s", display_text);

                if (list_index == highlight) {
                    wattroff(menu_win, A_REVERSE);
                }
            }
        }

        // Hilfetext am unteren Rand
        mvwprintw(menu_win, win_height - 1, 2, " UP/DOWN:Navgation Enter:Start ESC/q:Quit ");

        wrefresh(menu_win);

        // Tastatureingabe verarbeiten
        int c = wgetch(menu_win);

        if (c == KEY_UP && highlight > 0) {
            highlight--;
        }
        else if (c == KEY_DOWN && highlight < filtered_count - 1) {
            highlight++;
        }
        else if(c == KEY_NPAGE && highlight < filtered_count - 1){
            highlight += 5;
            if (highlight >= filtered_count) highlight = filtered_count - 1;
        }
        else if(c == KEY_PPAGE && highlight > 0){
            highlight -= 5;
            if (highlight < 0) highlight = 0;
        }
        else if (c == 10) {  // Enter
            choice = highlight;
            break;
        }
        else if (c == 27 || c == 'q') {  // ESC oder 'q'
            break;
        }
        else if (c == KEY_BACKSPACE || c == 127 || c == 8) {  // Backspace
            if (search_len > 0) {
                search[--search_len] = '\0';
                filtered_count = filter_apps(apps, app_count, filtered_indices, search);
                highlight = 0;
                offset = 0;
            }
        }
        else if (isprint(c) && search_len < MAX_SEARCH - 1) {  // Druckbare Zeichen
            search[search_len++] = c;
            search[search_len] = '\0';
            filtered_count = filter_apps(apps, app_count, filtered_indices, search);
            highlight = 0;
            offset = 0;
        }
    }

    // ncurses beenden
    endwin();

    // Ausgewählte App starten
    if (choice != -1 && choice < filtered_count) {
        int app_index = filtered_indices[choice];
        char command[1024];

        if (apps[app_index].is_flatpak) {
            char app_id[512];
            strncpy(app_id, apps[app_index].file_name, sizeof(app_id) - 1);
            char *ext = strstr(app_id, ".desktop");
            if (ext) *ext = '\0';

            snprintf(command, sizeof(command),
                    "flatpak run %s >/dev/null 2>&1 &", app_id);
        } else {
            snprintf(command, sizeof(command),
                    "gtk-launch %s >/dev/null 2>&1 &", apps[app_index].file_name);
        }        system(command);
    }

    // Speicher freigeben
    for (int i = 0; i < app_count; i++) {
        free(apps[i].display_name);
        free(apps[i].file_name);
    }

    return 0;
}
