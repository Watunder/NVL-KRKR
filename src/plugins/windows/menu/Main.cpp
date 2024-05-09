#include <windows.h>
#include "tp_stub.h"
#include "simplebinder.hpp"
#include "MenuItemIntf.h"
#include "resource.h"
#include <tchar.h>
#include <string.h>

const tjs_char* TVPSpecifyWindow = NULL;
const tjs_char* TVPSpecifyMenuItem = NULL;
const tjs_char* TVPInternalError = NULL;
const tjs_char* TVPNotChildMenuItem = NULL;
const tjs_char* TVPMenuIDOverflow = NULL;

static void LoadMessageFromResource() {
	static const int BUFF_SIZE = 1024;
	HINSTANCE hInstance = ::GetModuleHandle(_T("menu.dll"));
	TCHAR buffer[BUFF_SIZE];
	TCHAR* work;
	int len;

	len = ::LoadString( hInstance, IDS_SPECIFY_WINDOW, buffer, BUFF_SIZE );
	work = new TCHAR[len+1];
	_tcscpy_s( work, len+1, buffer );
	TVPSpecifyWindow = work;

	len = ::LoadString( hInstance, IDS_SPECIFY_MENU_ITEM, buffer, BUFF_SIZE );
	work = new TCHAR[len+1];
	_tcscpy_s( work, len+1, buffer );
	TVPSpecifyMenuItem = work;

	len = ::LoadString( hInstance, IDS_INTERNAL_ERROR, buffer, BUFF_SIZE );
	work = new TCHAR[len+1];
	_tcscpy_s( work, len+1, buffer );
	TVPInternalError = work;

	len = ::LoadString( hInstance, IDS_NOT_CHILD_MENU_ITEM, buffer, BUFF_SIZE );
	work = new TCHAR[len+1];
	_tcscpy_s( work, len+1, buffer );
	TVPNotChildMenuItem = work;
	
	len = ::LoadString( hInstance, IDS_MENU_ID_OVERFLOW, buffer, BUFF_SIZE );
	work = new TCHAR[len+1];
	_tcscpy_s( work, len+1, buffer );
	TVPMenuIDOverflow = work;
}
static void FreeMessage() {
	delete[] TVPSpecifyWindow;
	delete[] TVPSpecifyMenuItem;
	delete[] TVPInternalError;
	delete[] TVPNotChildMenuItem;
	delete[] TVPMenuIDOverflow;
	TVPSpecifyWindow = NULL;
	TVPSpecifyMenuItem = NULL;
	TVPInternalError = NULL;
	TVPNotChildMenuItem = NULL;
	TVPMenuIDOverflow = NULL;
}

static std::map<HWND,iTJSDispatch2*> MENU_LIST;
static void AddMenuDispatch( HWND hWnd, iTJSDispatch2* menu ) {
	MENU_LIST.insert( std::map<HWND, iTJSDispatch2*>::value_type( hWnd, menu ) );
}

static iTJSDispatch2* GetMenuDispatch( HWND hWnd ) {
	std::map<HWND, iTJSDispatch2*>::iterator i = MENU_LIST.find( hWnd );
	if( i != MENU_LIST.end() ) {
		return i->second;
	}
	return NULL;
}

static void DelMenuDispatch( HWND hWnd ) {
	MENU_LIST.erase(hWnd);
}

/**
 * メニューの中から既に存在しなくなったWindowについているメニューオブジェクトを削除する
 */
static void UpdateMenuList() {
	std::map<HWND, iTJSDispatch2*>::iterator i = MENU_LIST.begin();
	for( ; i != MENU_LIST.end(); ) {
		HWND hWnd = i->first;
		BOOL exist = ::IsWindow( hWnd );
		if( exist == 0 ) {
			// 既になくなったWindow
			std::map<HWND, iTJSDispatch2*>::iterator target = i;
			i++;
			iTJSDispatch2* menu = target->second;
			MENU_LIST.erase( target );
			menu->Release();
			TVPDeleteAcceleratorKeyTable( hWnd );
		} else {
			i++;
		}
	}
}

class WindowMenuProperty : public tTJSDispatch{
	tjs_error TJS_INTF_METHOD PropGet( tjs_uint32 flag,	const tjs_char * membername, tjs_uint32 *hint, tTJSVariant *result,	iTJSDispatch2 *objthis ) {
		tTJSVariant var;
		if( TJS_FAILED(objthis->PropGet(0, TJS_W("HWND"), NULL, &var, objthis)) ) {
			return TJS_E_INVALIDOBJECT;
		}
		HWND hWnd = (HWND)(tjs_int64)var;
		iTJSDispatch2* menu = GetMenuDispatch( hWnd );
		if( menu == NULL ) {
			UpdateMenuList();
			menu = TVPCreateMenuItemObject(objthis);
			AddMenuDispatch( hWnd, menu );
		}
		*result = tTJSVariant(menu, menu);
		return TJS_S_OK;
	}
	tjs_error TJS_INTF_METHOD PropSet( tjs_uint32 flag, const tjs_char *membername,	tjs_uint32 *hint, const tTJSVariant *param,	iTJSDispatch2 *objthis ) {
		return TJS_E_ACCESSDENYED;
	}
} *gWindowMenuProperty;

/**
 * キーコード文字列辞書／配列生成
 */
iTJSDispatch2* textToKeycodeMap = NULL;
iTJSDispatch2* keycodeToTextList = NULL;

static void ReleaseShortCutKeyCodeTable() {
	if( textToKeycodeMap ) textToKeycodeMap->Release();
	if( keycodeToTextList ) keycodeToTextList->Release();
	textToKeycodeMap = NULL;
	keycodeToTextList = NULL;
}

bool SetShortCutKeyCode(ttstr text, int key, bool force) {
	tTJSVariant vtext(text);
	tTJSVariant vkey(key);

	text.ToLowerCase();
	if( TJS_FAILED(textToKeycodeMap->PropSet(TJS_MEMBERENSURE, text.c_str(), NULL, &vkey, textToKeycodeMap)) )
		return false;
	if( force == false ) {
		tTJSVariant var;
		keycodeToTextList->PropGetByNum(0, key, &var, keycodeToTextList);
		if( var.Type() == tvtString ) return true;
	}
	return TJS_SUCCEEDED(keycodeToTextList->PropSetByNum(TJS_MEMBERENSURE, key, &vtext, keycodeToTextList));
}

static void CreateShortCutKeyCodeTable() {
	textToKeycodeMap = TJSCreateDictionaryObject();
	keycodeToTextList = TJSCreateArrayObject();
	if( textToKeycodeMap == NULL || keycodeToTextList == NULL ) return;

	TCHAR tempKeyText[32];
	for( int key = 8; key <= 255; key++ ) {
		int code = (::MapVirtualKey( key, 0 )<<16)|(1<<25);
		if( ::GetKeyNameText( code, tempKeyText, 32 ) > 0 ) {
			ttstr text(tempKeyText);
			// NumPadキー特殊処理
			if( TJS_strnicmp(text.c_str(), TJS_W("Num "), 4) == 0 ) {
				bool numpad = ( key >= VK_NUMPAD0 && key <= VK_DIVIDE );
				if( !numpad && ::GetKeyNameText( code|(1<<24), tempKeyText, 32 ) > 0 ) {
					text = tempKeyText;
				}
			}
			SetShortCutKeyCode(text, key, true);
		}
	}

	// 吉里吉里２互換用ショートカット文字列
	SetShortCutKeyCode(TJS_W("BkSp"), VK_BACK, false);
	SetShortCutKeyCode(TJS_W("PgUp"), VK_PRIOR, false);
	SetShortCutKeyCode(TJS_W("PgDn"), VK_NEXT, false);
}

tjs_error getMenuItem(tTJSVariant* result)
{
	iTJSDispatch2* tjsclass = TVPCreateNativeClass_MenuItem();
	*result = tTJSVariant(tjsclass);

	return TJS_S_OK;
}

tjs_error getMenu()
{
	tTJSVariant result;
	gWindowMenuProperty = new WindowMenuProperty();
	result = tTJSVariant(gWindowMenuProperty);

	iTJSDispatch2* Window = SimpleBinder::BindUtil::GetObjectW(TJS_W("Window"));
	Window->PropSet(TJS_MEMBERENSURE, TJS_W("menu"), NULL, &result, Window);

	result.Clear();

	return TJS_S_OK;
}

bool Entry(bool link)
{
	return
		(SimpleBinder::BindUtil(TJS_W(""), link)
			.Property(TJS_W("MenuItem"), &getMenuItem, NULL)
			.IsValid()
		);
}

bool onV2Link()
{
	LoadMessageFromResource();
	CreateShortCutKeyCodeTable();

	getMenu();

	return Entry(true);
}
bool onV2Unlink()
{ 
	ReleaseShortCutKeyCodeTable();
	FreeMessage();

	return Entry(false);
}
