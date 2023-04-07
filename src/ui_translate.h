#ifndef UI_TRANSLATE_H
#define UI_TRANSLATE_H

#include <string>

/**
 * Translation function: Call Translate signal on UI interface, which returns a boost::optional result.
 * If no translation slot is registered, nothing is returned, and simply return the input.
 */
std::string ui_translate(const char* psz);

#endif // UI_TRANSLATE_H
