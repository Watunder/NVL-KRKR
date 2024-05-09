#include <windows.h>
#include "tp_stub.h"
#include "simplebinder.hpp"
#include "KAGParser.h"

tjs_error getKAGParser(tTJSVariant* result)
{
	iTJSDispatch2* tjsclass = TVPCreateNativeClass_KAGParser();
	*result = tTJSVariant(tjsclass);

	return TJS_S_OK;
}

bool Entry(bool link)
{
	return
		(SimpleBinder::BindUtil(TJS_W(""), link)
			.Property(TJS_W("KAGParser"), &getKAGParser, NULL)
			.IsValid()
		);
}

bool onV2Link() { return Entry(true); }
bool onV2Unlink() { return Entry(false); }
