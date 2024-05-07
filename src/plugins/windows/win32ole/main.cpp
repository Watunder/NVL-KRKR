#include <windows.h>
#include <DispEx.h>
#include <stdio.h>

#define KRKRDISPWINDOWCLASS _T("TScrollBox")

// ATL
#if _MSC_VER == 1200
// Microsoft SDK のものとかちあうので排除
#define __IHTMLControlElement_INTERFACE_DEFINED__
#endif

#include "tp_stub.h"
#include "ncbind.hpp"
#include "simplebinder.hpp"

#include <atlbase.h>
static CComModule _Module;
#include <atlwin.h>
#include <atlcom.h>
#include <atliface.h>
#define _ATL_DLL
#include <atlhost.h>
#include <ExDispID.h>

// ログ出力用
static void log(const tjs_char *format, ...)
{
	va_list args;
	va_start(args, format);
	tjs_char msg[1024];
	_vsnwprintf_s(msg, 1024, format, args);
	TVPAddLog(msg);
	va_end(args);
}

//---------------------------------------------------------------------------

#include "IDispatchWrapper.hpp"

/**
 * OLE -> 吉里吉里 イベントディスパッチャ
 * sender (IUnknown) から DIID のイベントを受理し、
 * receiver (tTJSDispatch2) に送信する。
 */ 
class EventSink : public IDispatch
{
protected:
	int refCount;
	REFIID diid;
	ITypeInfo *pTypeInfo;
	iTJSDispatch2 *receiver;

public:
	EventSink(GUID diid, ITypeInfo *pTypeInfo, iTJSDispatch2 *receiver) : diid(diid), pTypeInfo(pTypeInfo), receiver(receiver) {
		refCount = 1;
		if (pTypeInfo) {
			pTypeInfo->AddRef();
		}
		if (receiver) {
			receiver->AddRef();
		}
	}

	~EventSink() {
		if (receiver) {
			receiver->Release();
		}
		if (pTypeInfo) {
			pTypeInfo->Release();
		}
	}

	//----------------------------------------------------------------------------
	// IUnknown 実装
	
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
											 void __RPC_FAR *__RPC_FAR *ppvObject) {
		if (riid == IID_IUnknown ||
			riid == IID_IDispatch||
			riid == diid) {
			if (ppvObject == NULL)
				return E_POINTER;
			*ppvObject = this;
			AddRef();
			return S_OK;
		} else {
			*ppvObject = 0;
			return E_NOINTERFACE;
		}
	}

	ULONG STDMETHODCALLTYPE AddRef() {
		refCount++;
		return refCount;
	}

	ULONG STDMETHODCALLTYPE Release() {
		int ret = --refCount;
		if (ret <= 0) {
			delete this;
			ret = 0;
		}
		return ret;
	}
	
	// -------------------------------------
	// IDispatch の実装
public:
	STDMETHOD (GetTypeInfoCount) (UINT* pctinfo)
	{
		return	E_NOTIMPL;
	}

	STDMETHOD (GetTypeInfo) (UINT itinfo, LCID lcid, ITypeInfo** pptinfo)
	{
		return	E_NOTIMPL;
	}

	STDMETHOD (GetIDsOfNames) (REFIID riid, LPOLESTR* rgszNames, UINT cNames, LCID lcid, DISPID* rgdispid)
	{
		return	E_NOTIMPL;
	}

	STDMETHOD (Invoke) (DISPID dispid, REFIID riid, LCID lcid, WORD wFlags, DISPPARAMS* pdispparams, VARIANT* pvarResult, EXCEPINFO* pexcepinfo, UINT* puArgErr)
	{
		BSTR bstr = NULL;
		if (pTypeInfo) {
			unsigned int len;
			pTypeInfo->GetNames(dispid, &bstr, 1, &len);
		}
		HRESULT hr = IDispatchWrapper::InvokeEx(receiver, bstr, wFlags, pdispparams, pvarResult, pexcepinfo);
		if (hr == DISP_E_MEMBERNOTFOUND) {
			//log(L"member not found:%ws", bstr);
			hr = S_OK;
		}
		if (bstr) {
			SysFreeString(bstr);
		}
		return hr;
	}
};

//---------------------------------------------------------------------------

/*
 * WIN32OLE ネイティブインスタンス
 */
class WIN32OLE // ネイティブインスタンス
{
public:
	iTJSDispatch2 *objthis; //< 自分自身
	IDispatch *pDispatch; //< 保持してるインスタンス

protected:

	struct EventInfo {
		IID diid;
		DWORD cookie;
		EventInfo(REFIID diid, DWORD cookie) : diid(diid), cookie(cookie) {};
	};
	vector<EventInfo> events;

	/**
	 * イベント情報の消去
	 */
	void clearEvent() {
		if (pDispatch) {
			vector<EventInfo>::iterator i = events.begin();
			while (i != events.end()) {
				AtlUnadvise(pDispatch, i->diid, i->cookie);
				i++;
			}
			events.clear();
		}
	}

	/**
	 * 登録情報の消去
	 */
	void clear() {
		clearEvent();
		if (pDispatch) {
			pDispatch->Release();
			pDispatch = NULL;
		}
	}

public:
	/**
	 * コンストラクタ
	 * @param objthis TJS2インスタンス
	 * @param progIdorCLSID
	 */
	WIN32OLE(iTJSDispatch2 *objthis, const tjs_char *progIdOrCLSID) : objthis(objthis), pDispatch(NULL) {
		if (progIdOrCLSID) {
			HRESULT hr;
			CLSID   clsid;
			OLECHAR *oleName = SysAllocString(progIdOrCLSID);
			if (FAILED(hr = CLSIDFromProgID(oleName, &clsid))) {
				hr = CLSIDFromString(oleName, &clsid);
			}
			SysFreeString(oleName);
			if (SUCCEEDED(hr)) {
				// COM 接続してIDispatch を取得する
				/* get IDispatch interface */
				hr = CoCreateInstance(clsid, NULL, CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER, IID_IDispatch, (void**)&pDispatch);
				if (!SUCCEEDED(hr)) {
					log(L"CoCreateInstance failed %ws", progIdOrCLSID);
				}
			} else {
				log(L"bad CLSID %ws", progIdOrCLSID);
			}
		}
		tTJSVariant name(TJS_W("missing"));
		objthis->ClassInstanceInfo(TJS_CII_SET_MISSING, 0, &name);
	}
	
	// オブジェクトが無効化されるときに呼ばれる
	virtual ~WIN32OLE()	{
		clear();
	}

	/**
	 * インスタンス生成ファクトリ
	 */
	static tjs_error factory(iTJSDispatch2 *objthis, WIN32OLE *&instance, tjs_int numparams, tTJSVariant **param) {
		if (numparams < 1) {
			return TJS_E_BADPARAMCOUNT;
		}

		instance = new WIN32OLE(objthis, param[0]->GetString());

		return S_OK;
	}

protected:

	/**
	 * メソッド実行
	 * メンバ名を直接指定
	 * @param wFlag 実行フラグ
	 * @param membername メンバ名
	 * @param result 結果
	 * @param numparams 引数の数
	 * @param param 引数
	 */
	tjs_error invoke(DWORD wFlags,
					 const tjs_char *membername,
					 tTJSVariant *result,
					 tjs_int numparams,
					 tTJSVariant **param) {
		if (pDispatch) {
			return iTJSDispatch2Wrapper::Invoke(pDispatch,
												wFlags,
												membername,
												result,
												numparams,
												param);
		}
		return TJS_E_FAIL;
	}
	
	/**
	 * メソッド実行
	 * パラメータの１つ目がメソッド名
	 * @param wFlag 実行フラグ
	 * @param result 結果
	 * @param numparams 引数の数
	 * @param param 引数
	 */
	tjs_error invoke(DWORD wFlags,
					 tTJSVariant *result,
					 tjs_int numparams,
					 tTJSVariant **param) {
		//log(L"native invoke %d", numparams);
		if (pDispatch) {
			if (numparams > 0) {
				if (param[0]->Type() == tvtString) {
					return iTJSDispatch2Wrapper::Invoke(pDispatch,
														wFlags,
														param[0]->GetString(),
														result,
														numparams - 1,
														param ? param + 1 : NULL);
				} else {
					return TJS_E_INVALIDPARAM;
				}
			} else {
				return TJS_E_BADPARAMCOUNT;
			}
		}
		return TJS_E_FAIL;
	}

	/**
	 * メソッド実行
	 */
	tjs_error missing(tTJSVariant *result, tjs_int numparams, tTJSVariant **param) {
		
		if (numparams < 3) {return TJS_E_BADPARAMCOUNT;};
		bool ret = false;
		const tjs_char *membername = param[1]->GetString();
		if ((int)*param[0]) {
			// put
			ret = TJS_SUCCEEDED(invoke(DISPATCH_PROPERTYPUT, membername, NULL, 1, &param[2]));
		} else {
			// get
			tTJSVariant result;
			tjs_error err;
			ret = TJS_SUCCEEDED(err = invoke(DISPATCH_PROPERTYGET|DISPATCH_METHOD, membername, &result, 0, NULL));
			if (err == TJS_E_BADPARAMCOUNT) {
				result = new iTJSDispatch2WrapperForMethod(pDispatch, membername);
				ret = true;
			}
			if (ret) {
				iTJSDispatch2 *value = param[2]->AsObject();
				if (value) {
					value->PropSet(0, NULL, NULL, &result, NULL);
					value->Release();
				}
			}
		}
		if (result) {
			*result = ret;
		}
		return TJS_S_OK;
	}

public:
	static tjs_error invokeMethod(WIN32OLE* self, tTJSVariant *result, tjs_int numparams, tTJSVariant **param) {
		return self->invoke(DISPATCH_PROPERTYGET|DISPATCH_METHOD, result, numparams, param);
	}
	
	static tjs_error setMethod(WIN32OLE* self, tTJSVariant *result, tjs_int numparams, tTJSVariant **param) {
		return self->invoke(DISPATCH_PROPERTYPUT, result, numparams, param);
	}
	
	static tjs_error getMethod(WIN32OLE* self, tTJSVariant *result, tjs_int numparams, tTJSVariant **param) {
		return self->invoke(DISPATCH_PROPERTYGET, result, numparams, param);
	}

	static tjs_error missingMethod(WIN32OLE* self, tTJSVariant *result, tjs_int numparams, tTJSVariant **param) {
		return self->missing(result, numparams, param);
	}
	
protected:

	/**
	 * デフォルトの IID を探す
	 * @param pitf 名前
	 * @param piid 取得したIIDの格納先
	 * @param ppTypeInfo 関連する方情報
	 */
	
	HRESULT findDefaultIID(IID *piid, ITypeInfo **ppTypeInfo) {

		HRESULT hr;

		IProvideClassInfo2 *pProvideClassInfo2;
		hr = pDispatch->QueryInterface(IID_IProvideClassInfo2, (void**)&pProvideClassInfo2);
		if (SUCCEEDED(hr)) {
			hr = pProvideClassInfo2->GetGUID(GUIDKIND_DEFAULT_SOURCE_DISP_IID, piid);
			pProvideClassInfo2->Release();
			ITypeInfo *pTypeInfo;
			if (SUCCEEDED(hr = pDispatch->GetTypeInfo(0, LOCALE_SYSTEM_DEFAULT, &pTypeInfo))) {
				ITypeLib *pTypeLib;
				unsigned int index;
				if (SUCCEEDED(hr = pTypeInfo->GetContainingTypeLib(&pTypeLib, &index))) {
					hr = pTypeLib->GetTypeInfoOfGuid(*piid, ppTypeInfo);
				}
			}
			return hr;
		}

		IProvideClassInfo *pProvideClassInfo;
		if (SUCCEEDED(hr = pDispatch->QueryInterface(IID_IProvideClassInfo, (void**)&pProvideClassInfo))) {
			ITypeInfo *pTypeInfo;
			if (SUCCEEDED(hr = pProvideClassInfo->GetClassInfo(&pTypeInfo))) {
				
				TYPEATTR *pTypeAttr;
				if (SUCCEEDED(hr = pTypeInfo->GetTypeAttr(&pTypeAttr))) {
					int i;
					for (i = 0; i < pTypeAttr->cImplTypes; i++) {
						int iFlags;
						if (SUCCEEDED(hr = pTypeInfo->GetImplTypeFlags(i, &iFlags))) {
							if ((iFlags & IMPLTYPEFLAG_FDEFAULT) &&	(iFlags & IMPLTYPEFLAG_FSOURCE)) {
								HREFTYPE hRefType;
								if (SUCCEEDED(hr = pTypeInfo->GetRefTypeOfImplType(i, &hRefType))) {
									if (SUCCEEDED(hr = pTypeInfo->GetRefTypeInfo(hRefType, ppTypeInfo))) {
										break;
									}
								}
							}
						}
					}
					pTypeInfo->ReleaseTypeAttr(pTypeAttr);
				}
				pTypeInfo->Release();
			}
			pProvideClassInfo->Release();
		}

		if (!*ppTypeInfo) {
			if (SUCCEEDED(hr)) {
				hr = E_UNEXPECTED;
			}
		} else {
			TYPEATTR *pTypeAttr;
			hr = (*ppTypeInfo)->GetTypeAttr(&pTypeAttr);
			if (SUCCEEDED(hr)) {
				*piid = pTypeAttr->guid;
				(*ppTypeInfo)->ReleaseTypeAttr(pTypeAttr);
			} else {
				(*ppTypeInfo)->Release();
				*ppTypeInfo = NULL;
			}
		}
		return hr;
	}
	
	/**
	 * IID を探す
	 * @param pitf 名前
	 * @param piid 取得したIIDの格納先
	 * @param ppTypeInfo 関連する方情報
	 */
	HRESULT findIID(const tjs_char *pitf, IID *piid, ITypeInfo **ppTypeInfo) {

		if (pitf == NULL) {
			return findDefaultIID(piid, ppTypeInfo);
		}

		HRESULT hr;
		ITypeInfo *pTypeInfo;
		if (SUCCEEDED(hr = pDispatch->GetTypeInfo(0, LOCALE_SYSTEM_DEFAULT, &pTypeInfo))) {
			ITypeLib *pTypeLib;
			unsigned int index;
			if (SUCCEEDED(hr = pTypeInfo->GetContainingTypeLib(&pTypeLib, &index))) {
				bool found = false;
				unsigned int count = pTypeLib->GetTypeInfoCount();
				for (index = 0; index < count; index++) {
					ITypeInfo *pTypeInfo;
					if (SUCCEEDED(pTypeLib->GetTypeInfo(index, &pTypeInfo))) {
						TYPEATTR *pTypeAttr;
						if (SUCCEEDED(pTypeInfo->GetTypeAttr(&pTypeAttr))) {
							if (pTypeAttr->typekind == TKIND_COCLASS) {
								int type;
								for (type = 0; !found && type < pTypeAttr->cImplTypes; type++) {
									HREFTYPE RefType;
									if (SUCCEEDED(pTypeInfo->GetRefTypeOfImplType(type, &RefType))) {
										ITypeInfo *pImplTypeInfo;
										if (SUCCEEDED(pTypeInfo->GetRefTypeInfo(RefType, &pImplTypeInfo))) {
											BSTR bstr = NULL;
											if (SUCCEEDED(pImplTypeInfo->GetDocumentation(-1, &bstr, NULL, NULL, NULL))) {
												if (wcscmp(pitf, bstr) == 0) {
													TYPEATTR *pImplTypeAttr;
													if (SUCCEEDED(pImplTypeInfo->GetTypeAttr(&pImplTypeAttr))) {
														found = true;
														*piid = pImplTypeAttr->guid;
														if (ppTypeInfo) {
															*ppTypeInfo = pImplTypeInfo;
															(*ppTypeInfo)->AddRef();
														}
														pImplTypeInfo->ReleaseTypeAttr(pImplTypeAttr);
													}
												}
												SysFreeString(bstr);
											}
											pImplTypeInfo->Release();
										}
									}
								}
							}
							pTypeInfo->ReleaseTypeAttr(pTypeAttr);
						}
						pTypeInfo->Release();
					}
					if (found) {
						break;
					}
				}
				if (!found) {
					hr = E_NOINTERFACE;
				}
				pTypeLib->Release();
			}
			pTypeInfo->Release();
		}
		return hr;
	}

	/**
	 * イベントを登録
	 */
	bool addEvent(const tjs_char *diidName, iTJSDispatch2 *receiver) {
		bool ret = false;
		IID diid;
		ITypeInfo *pTypeInfo;
		if (SUCCEEDED(findIID(diidName, &diid, &pTypeInfo))) {
			EventSink *sink = new EventSink(diid, pTypeInfo, receiver);
			DWORD cookie;
			if (SUCCEEDED(AtlAdvise(pDispatch, sink, diid, &cookie))) {
				events.push_back(EventInfo(diid, cookie));
				ret = true;
			}
			sink->Release();
			if (pTypeInfo) {
				pTypeInfo->Release();
			}
		}
		return ret;
	}

	/**
	 * イベント登録
	 */
	static tjs_error _addEventMethod(tTJSVariant *result, tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *objthis, WIN32OLE *self) {
		if (numparams < 1) {
			return TJS_E_BADPARAMCOUNT;
		}
		if (!self) {
			return TJS_E_NATIVECLASSCRASH;
		}
		bool success = false;
		const tjs_char *diidName = param[0]->GetString();
		if (numparams > 1) {
			iTJSDispatch2 *receiver = param[1]->AsObject();
			if (receiver) {
				success = self->addEvent(diidName, receiver);
				receiver->Release();
			}
		} else {
			success = self->addEvent(diidName, objthis);
		}
		if (!success) {
			log(L"イベント[%ws]の登録に失敗しました", diidName);
		}
		return TJS_S_OK;
	}

	/**
	 * 定数の取得
	 * @param pTypeInfo TYPEINFO
	 * @param target 格納先
	 */
	void getConstant(ITypeInfo *pTypeInfo, iTJSDispatch2 *target) {
		// 個数情報
		TYPEATTR  *pTypeAttr = NULL;
		if (SUCCEEDED(pTypeInfo->GetTypeAttr(&pTypeAttr))) {
			for (int i=0; i<pTypeAttr->cVars; i++) {
				VARDESC *pVarDesc = NULL;
				if (SUCCEEDED(pTypeInfo->GetVarDesc(i, &pVarDesc))) {
					if (pVarDesc->varkind == VAR_CONST &&
						!(pVarDesc->wVarFlags & (VARFLAG_FHIDDEN | VARFLAG_FRESTRICTED | VARFLAG_FNONBROWSABLE))) {
						BSTR bstr = NULL;
						unsigned int len;
						if (SUCCEEDED(pTypeInfo->GetNames(pVarDesc->memid, &bstr, 1, &len)) && len >= 0 && bstr) {
							//log(L"const:%s", bstr);
							tTJSVariant result;
							IDispatchWrapper::storeVariant(result, *(pVarDesc->lpvarValue));
							target->PropSet(TJS_MEMBERENSURE|TJS_IGNOREPROP,
											bstr,
											NULL,
											&result,
											target
											);
							SysFreeString(bstr);
						}
					}
					pTypeInfo->ReleaseVarDesc(pVarDesc);
				}
			}
			pTypeInfo->ReleaseTypeAttr(pTypeAttr);
		}
	}

	/**
	 * 定数の取得
	 * @param pTypeLib TYPELIB
	 * @param target 格納先
	 */
	void getConstant(ITypeLib *pTypeLib, iTJSDispatch2 *target) {
		unsigned int count = pTypeLib->GetTypeInfoCount();
		for (unsigned int i=0; i<count; i++) {
			ITypeInfo *pTypeInfo = NULL;
			if (SUCCEEDED(pTypeLib->GetTypeInfo(i, &pTypeInfo))) {
				getConstant(pTypeInfo, target);
				pTypeInfo->Release();
			}
		}
	}

	/**
	 * 定数の取得
	 * @param target 格納先
	 */
	void getConstant(iTJSDispatch2 *target) {
		if (pDispatch) {
			if (target) {
				ITypeInfo *pTypeInfo = NULL;
				if (SUCCEEDED(pDispatch->GetTypeInfo(0, LOCALE_SYSTEM_DEFAULT, &pTypeInfo))) {
					unsigned int index = 0;
					ITypeLib *pTypeLib = NULL;
					if (SUCCEEDED(pTypeInfo->GetContainingTypeLib(&pTypeLib, &index))) {
						getConstant(pTypeLib, target);
						pTypeLib->Release();
					}
					pTypeInfo->Release();
				}
			}
		}
	}

	/**
	 * メソッド実行
	 */
	static tjs_error _getConstantMethod(tTJSVariant *result, tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *objthis, WIN32OLE *self) {
		if (!self) {
			return TJS_E_NATIVECLASSCRASH;
		}
		if (numparams > 0) {
			iTJSDispatch2 *store = param[0]->AsObject();
			if (store) {
				self->getConstant(store);
				store->Release();
			}
		} else {
			self->getConstant(objthis);
		}
		return TJS_S_OK;
	}

public:
	static tjs_error addEventMethod(iTJSDispatch2* objthis, tTJSVariant *result, tjs_int numparams, tTJSVariant **param) {
		WIN32OLE *self = ncbInstanceAdaptor<WIN32OLE>::GetNativeInstance(objthis);
		return _addEventMethod(result, numparams, param, objthis, self);
	}
	
	static tjs_error getConstantMethod(iTJSDispatch2* objthis, tTJSVariant *result, tjs_int numparams, tTJSVariant **param) {
		WIN32OLE *self = ncbInstanceAdaptor<WIN32OLE>::GetNativeInstance(objthis);
		return _getConstantMethod(result, numparams, param, objthis, self);
	}
};

//---------------------------------------------------------------------------

// 吉里吉里のアーカイブにアクセスするための処理
void registerArchive();
void unregisterArchive();

static BOOL gOLEInitialized = false;

/**
 * 登録処理前
 */
static void PreRegistCallback()
{
	if (!gOLEInitialized) {
		if (SUCCEEDED(OleInitialize(NULL))) {
			gOLEInitialized = true;
		} else {
			log(L"OLE 初期化失敗");
		}
	}
	
	// アーカイブ処理
	registerArchive();
	
	// ATL関連初期化
	_Module.Init(NULL, NULL);
	AtlAxWinInit();
}

/**
 * 開放処理後
 */
static void PostUnregistCallback()
{
	// ATL 終了
	_Module.Term();

	// アーカイブ終了
	unregisterArchive();
	
	if (gOLEInitialized) {
		OleUninitialize();
		gOLEInitialized = false;
	}
}

bool Entry(bool link)
{
	return
		(SimpleBinder::BindUtil(TJS_W(""), link)
			.Class(TJS_W("WIN32OLE"), &WIN32OLE::factory)
			.Function(TJS_W("invoke"), &WIN32OLE::invokeMethod)
			.Function(TJS_W("set"), &WIN32OLE::setMethod)
			.Function(TJS_W("get"), &WIN32OLE::getMethod)
			.Function(TJS_W("missing"), &WIN32OLE::missingMethod)
			.Function(TJS_W("addEvent"), &WIN32OLE::addEventMethod)
			.Function(TJS_W("getConstant"), &WIN32OLE::getConstantMethod)
			.IsValid()
		);
}

bool onV2Link() { 
	PreRegistCallback();
	return Entry(true); 
}
bool onV2Unlink() {
	PostUnregistCallback();
	return Entry(false);
}
