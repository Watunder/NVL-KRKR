//---------------------------------------------------------------------------
#include <windows.h>
#include "tp_stub.h"
#include "simplebinder.hpp"
//---------------------------------------------------------------------------



//---------------------------------------------------------------------------
// 指定されたディレクトリ内のファイルの一覧を得る関数
//---------------------------------------------------------------------------
static tjs_error getDirList(tTJSVariant *result, tjs_int numparams, tTJSVariant **param)
{
	// 引数 : ディレクトリ
	if (numparams < 1)
	{
		return TJS_E_BADPARAMCOUNT;
	}

	ttstr dir(*param[0]);

	if (dir.GetLastChar() != TJS_W('/'))
	{
		TVPThrowExceptionMessage(TJS_W("'/' must be specified at the end of given directory name."));
	}

	// OSネイティブな表現に変換
	dir = TVPNormalizeStorageName(dir);
	TVPGetLocalName(dir);

	// Array クラスのオブジェクトを作成
	iTJSDispatch2 * array;

	{
		tTJSVariant result;
		TVPExecuteExpression(TJS_W("[]"), &result);
		// なにか TJS スクリプトで出来そうなことをC++でやるのが面倒ならば
		// このように TJS 式を実行してしまうのが手っ取り早い
		array = result.AsObject();
	}

	try
	{
		// FindFirstFile を使ってファイルを列挙
		ttstr wildcard( dir + TJS_W("*.*") );

		WIN32_FIND_DATA data;
		HANDLE handle = FindFirstFile(wildcard.c_str(), &data);
		if (handle != INVALID_HANDLE_VALUE)
		{
			tjs_int count = 0;
			do
			{
				ttstr filepah( dir + data.cFileName );
				if (GetFileAttributes(filepah.c_str()) & FILE_ATTRIBUTE_DIRECTORY)
				{
					// ディレクトリの場合は最後に / をつける
					filepah = ttstr(data.cFileName) + TJS_W("/");
				}
				else
				{
					// 普通のファイルの場合はそのまま
					filepah = ttstr(data.cFileName);
				}

				// 配列に追加する
				tTJSVariant val(filepah);
				array->PropSetByNum(0, count++, &val, array);

			}
			while (FindNextFile(handle, &data));
			FindClose(handle);
		}
		else
		{
			TVPThrowExceptionMessage(TJS_W("Directory not found."));
		}

		if(result)
			*result = tTJSVariant(array, array);
	}
	catch(...)
	{
		array->Release();
		throw;
	}

	array->Release();

	// 戻る
	return TJS_S_OK;
}
//---------------------------------------------------------------------------

bool Entry(bool link)
{
	return
		(SimpleBinder::BindUtil(TJS_W(""), link)
			.Function(TJS_W("getDirList"), &getDirList)
			.IsValid()
		);
}

bool onV2Link() { return Entry(true); }
bool onV2Unlink() { return Entry(false); }
