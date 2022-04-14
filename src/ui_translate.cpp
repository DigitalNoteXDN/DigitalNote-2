#include "ui_interface.h"

#include "ui_translate.h"

/**
 * Translation function: Call Translate signal on UI interface, which returns a boost::optional result.
 * If no translation slot is registered, nothing is returned, and simply return the input.
 */
std::string ui_translate(const char* psz)
{
	boost::optional<std::string> rv = uiInterface.Translate(psz);

	return rv ? (*rv) : psz;
}

