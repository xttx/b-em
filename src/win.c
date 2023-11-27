/* B-em by Tom Walker, Allegro 5 port.
 *
 * Previously containing a Windows-specific main function this file now
 * B-Em specific functions whose definition varies between Windows and
 * Linux/Linux-like systems.
 */

#ifdef WIN32
#include "b-em.h"

static bool win_file_exists(ALLEGRO_PATH *path, const char *name, const char *ext) {
    const char *cpath;
    DWORD dwAttrib;

    al_set_path_filename(path, name);
    al_set_path_extension(path, ext);
    cpath = al_path_cstr(path, ALLEGRO_NATIVE_PATH_SEP);
    dwAttrib = GetFileAttributes(cpath);
    if (dwAttrib != INVALID_FILE_ATTRIBUTES && !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY)) {
        log_debug("win: %s found at '%s'", name, cpath);
        return true;
    }
    else {
        log_debug("win: %s not found at '%s'", name, cpath);
        return false;
    }
}

bool check_portable_txt() {
    ALLEGRO_PATH* path = al_get_standard_path(ALLEGRO_RESOURCES_PATH);
    bool exist = win_file_exists(path, "portable", ".txt");
    return exist;
}

ALLEGRO_PATH *find_dat_file(ALLEGRO_PATH *dir, const char *name, const char *ext) {
    ALLEGRO_PATH *path;

    if ((path = al_get_standard_path(ALLEGRO_RESOURCES_PATH))) {
        al_join_paths(path, dir);
        if (win_file_exists(path, name, ext))
            return path;
        al_destroy_path(path);
    }
    return NULL;
}

ALLEGRO_PATH *find_cfg_file(const char *name, const char *ext) {
    ALLEGRO_PATH *path;

    if (!portable_mode) {
        if ((path = al_get_standard_path(ALLEGRO_USER_SETTINGS_PATH))) {
            if (win_file_exists(path, name, ext))
                return path;
            al_destroy_path(path);
        }
    }

    if ((path = al_get_standard_path(ALLEGRO_RESOURCES_PATH))) {
        if (win_file_exists(path, name, ext))
            return path;
        al_destroy_path(path);
    }
    return NULL;
}

ALLEGRO_PATH *find_cfg_dest(const char *name, const char *ext) {
    ALLEGRO_PATH *path;
    const char *cpath;

    if (portable_mode) {
        path = al_get_standard_path(ALLEGRO_RESOURCES_PATH);
    } else {
        path = al_get_standard_path(ALLEGRO_USER_SETTINGS_PATH);
    }

    if (path) {
        cpath = al_path_cstr(path, ALLEGRO_NATIVE_PATH_SEP);
        CreateDirectory(cpath, NULL);
        al_set_path_filename(path, name);
        al_set_path_extension(path, ext);
        return path;
    }
    return NULL;
}

bool is_relative_filename(const char *fn)
{
    int c0 = *fn;
    return !(c0 == '/' || c0 == '\\' || (isalpha(c0) && fn[1] == ':'));
}

#ifndef __GNUC__
// https://stackoverflow.com/a/19932364/433626
int ftruncate(int fd, off_t length) {
    HANDLE handle = (HANDLE) _get_osfhandle(_fileno(fd));
    SetFilePointer(handle, length, 0, FILE_BEGIN);
    SetEndOfFile(handle);
}
#endif

#endif
