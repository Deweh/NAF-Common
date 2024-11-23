#include "Impl.h"

Settings::ImplFunctions& Settings::GetImplFunctions()
{
	static Settings::ImplFunctions instance;
	return instance;
}
