#ifndef LANG_H
#define LANG_H

#include <string>
#include <cstring>

#define TR(s) Translations::translate(s)

extern bool non_latin_filenames;

namespace Translations{
    extern bool loadLanguage(std::string lang_file);
    extern std::string translate(std::string orig);
};

#endif