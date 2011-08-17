// Copyright 2005-2009 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
// ========================================================================
//
// Reuses most of the functionality of the classes within atlmfc_vc80 statreg.h.
// The only functional difference is that GenerateError() returns a unique
// HRESULT based on the component CLSID/APPID as well as one of the E_ATL_XXX
// codes, instead of the generic DISP_E_EXCEPTION.

#ifndef OMAHA_BASE_STATREGEX_H__
#define OMAHA_BASE_STATREGEX_H__

#include <atlbase.h>
#include <statreg.h>

namespace omaha {

class RegObject;

class RegParser
{
public:
  RegParser(RegObject* pRegObj);

  HRESULT  PreProcessBuffer(__in_z LPTSTR lpszReg, __deref_out_z LPTSTR* ppszReg);
  HRESULT  RegisterBuffer(__in_z LPTSTR szReg, BOOL bRegister);

protected:

  void    SkipWhiteSpace();
  HRESULT NextToken(__out_ecount_z(MAX_VALUE) LPTSTR szToken);
  HRESULT AddValue(__in CRegKey& rkParent, __in_z_opt LPCTSTR szValueName, __out_ecount_z(MAX_VALUE) LPTSTR szToken);
  BOOL    CanForceRemoveKey(LPCTSTR szKey);
  BOOL    HasSubKeys(HKEY hkey);
  BOOL    HasValues(HKEY hkey);
  HRESULT RegisterSubkeys(__out_ecount_z(MAX_VALUE) LPTSTR szToken, __in HKEY hkParent, __in BOOL bRegister, __in BOOL bInRecovery = FALSE);
  BOOL    IsSpace(TCHAR ch);
  LPTSTR  m_pchCur;

  RegObject*     m_pRegObj;
  unsigned long guid_prefix_;

  HRESULT GenerateError(UINT registrar_error) {
    return MAKE_HRESULT(SEVERITY_ERROR, registrar_error, guid_prefix_);
  }

  //HRESULT HandleReplacements(LPTSTR& szToken);
  HRESULT SkipAssignment(__inout_ecount_z(MAX_VALUE) LPTSTR szToken);

  BOOL    EndOfVar() { return chQuote == *m_pchCur && chQuote != *CharNext(m_pchCur); }
  static LPTSTR StrChr(__in_z LPTSTR lpsz, __in TCHAR ch);
  static HKEY HKeyFromString(__in_z LPTSTR szToken);
  static BYTE ChToByte(const TCHAR ch);
  static BOOL VTFromRegType(LPCTSTR szValueType, VARTYPE& vt);
  static const TCHAR* const rgszNeverDelete[];
  static const int cbNeverDelete;
  static const int MAX_VALUE = 4096;
  static const int MAX_TYPE = 4096;

  // Implementation Helper
  class CParseBuffer
  {
  public:
    int nPos;
    int nSize;
    LPTSTR p;
    CParseBuffer(int nInitial)
    {
      if (nInitial < 100)
        nInitial = 1000;
      nPos = 0;
      nSize = nInitial;
      p = (LPTSTR) ::ATL::AtlCoTaskMemCAlloc(nSize,static_cast<ULONG>(sizeof(TCHAR)));
      if (p != NULL)
        *p = NULL;
    }
    ~CParseBuffer()
    {
      CoTaskMemFree(p);
    }
    BOOL Append(const TCHAR* pch, int nChars)
    {
      ATLASSERT(p != NULL);
      int newSize = nPos + nChars + 1;
      if ((newSize <= nPos) || (newSize <= nChars))
        return FALSE;

      if (newSize >= nSize)
      {
        while (newSize >= nSize) {
          if (nSize > INT_MAX / 2)
          return FALSE;
          nSize *= 2;
        }
        LPTSTR pTemp = (LPTSTR)::ATL::AtlCoTaskMemRecalloc(p, nSize, sizeof(TCHAR));
        if (pTemp == NULL)
          return FALSE;
        p = pTemp;
      }
      if ((nPos < 0) || (nPos >= nSize) || nSize - nPos > nSize)
        return FALSE;

#pragma warning(push)
#pragma warning(disable: 22008)
      /* Prefast false warning is fired here despite the all above checks */
      Checked::memcpy_s(p + nPos, (nSize-nPos) * sizeof(TCHAR), pch, int(nChars * sizeof(TCHAR)));
      nPos += nChars;
      *(p + nPos) = NULL;
#pragma warning(pop)
      return TRUE;
    }

    BOOL AddChar(const TCHAR* pch)
    {
#ifndef _UNICODE
      int nChars = int(CharNext(pch) - pch);
#else
      int nChars = 1;
#endif
      return Append(pch, nChars);

    }
    BOOL AddString(LPCOLESTR lpsz)
    {
      if (lpsz == NULL)
      {
        return FALSE;
      }
      USES_CONVERSION_EX;
      LPCTSTR lpszT = OLE2CT_EX(lpsz, _ATL_SAFE_ALLOCA_DEF_THRESHOLD);
      if (lpszT == NULL)
      {
        return FALSE;
      }
      return Append(lpszT, (int)lstrlen(lpszT));
    }
    LPTSTR Detach()
    {
      LPTSTR lp = p;
      p = NULL;
      nSize = nPos = 0;
      return lp;
    }

  };
};

class RegObject  : public IRegistrarBase
{
public:

  STDMETHOD(QueryInterface)(const IID &,void ** )
  {
    ATLASSERT(_T("statically linked in RegObject is not a com object. Do not callthis function"));
    return E_NOTIMPL;
  }

  STDMETHOD_(ULONG, AddRef)(void)
  {
    ATLASSERT(_T("statically linked in RegObject is not a com object. Do not callthis function"));
    return 1;
  }
  STDMETHOD_(ULONG, Release)(void)
  {
    ATLASSERT(_T("statically linked in RegObject is not a com object. Do not callthis function"));
    return 0;
  }

  virtual ~RegObject(){ClearReplacements();}
  HRESULT FinalConstruct() { return m_csMap.Init(); }
  void FinalRelease() {}

  // Map based methods
  HRESULT STDMETHODCALLTYPE AddReplacement(LPCOLESTR lpszKey, LPCOLESTR lpszItem);
  HRESULT STDMETHODCALLTYPE ClearReplacements();
  LPCOLESTR StrFromMap(__in_z LPTSTR lpszKey);

  // Register via a given mechanism
  HRESULT STDMETHODCALLTYPE ResourceRegister(LPCOLESTR pszFileName, UINT nID, LPCOLESTR pszType);
  HRESULT STDMETHODCALLTYPE ResourceRegisterSz(LPCOLESTR pszFileName, LPCOLESTR pszID, LPCOLESTR pszType);
  HRESULT STDMETHODCALLTYPE ResourceUnregister(LPCOLESTR pszFileName, UINT nID, LPCOLESTR pszType);
  HRESULT STDMETHODCALLTYPE ResourceUnregisterSz(LPCOLESTR pszFileName, LPCOLESTR pszID, LPCOLESTR pszType);
  HRESULT STDMETHODCALLTYPE FileRegister(LPCOLESTR bstrFileName)
  {
    return CommonFileRegister(bstrFileName, TRUE);
  }

  HRESULT STDMETHODCALLTYPE FileUnregister(LPCOLESTR bstrFileName)
  {
    return CommonFileRegister(bstrFileName, FALSE);
  }

  HRESULT STDMETHODCALLTYPE StringRegister(LPCOLESTR bstrData)
  {
    return RegisterWithString(bstrData, TRUE);
  }

  HRESULT STDMETHODCALLTYPE StringUnregister(LPCOLESTR bstrData)
  {
    return RegisterWithString(bstrData, FALSE);
  }

protected:

  HRESULT CommonFileRegister(LPCOLESTR pszFileName, BOOL bRegister);
  HRESULT RegisterFromResource(LPCOLESTR pszFileName, LPCTSTR pszID, LPCTSTR pszType, BOOL bRegister);
  HRESULT RegisterWithString(LPCOLESTR pszData, BOOL bRegister);

  static HRESULT GenerateError(UINT) {return DISP_E_EXCEPTION;}

  CExpansionVector                m_RepMap;
  CComObjectThreadModel::AutoDeleteCriticalSection      m_csMap;
};

inline HRESULT STDMETHODCALLTYPE RegObject::AddReplacement(LPCOLESTR lpszKey, LPCOLESTR lpszItem)
{
  if (lpszKey == NULL || lpszItem == NULL)
    return E_INVALIDARG;
  m_csMap.Lock();
  USES_CONVERSION_EX;

  LPCTSTR lpszT = OLE2CT_EX(lpszKey, _ATL_SAFE_ALLOCA_DEF_THRESHOLD);

#ifndef _UNICODE
  if(lpszT == NULL)
    return E_OUTOFMEMORY;
#endif

  BOOL bRet = m_RepMap.Add(lpszT, lpszItem);

  m_csMap.Unlock();
  return bRet ? S_OK : E_OUTOFMEMORY;
}

inline HRESULT RegObject::RegisterFromResource(LPCOLESTR bstrFileName, LPCTSTR szID,
                     LPCTSTR szType, BOOL bRegister)
{
  USES_CONVERSION_EX;

  HRESULT     hr;
  RegParser  parser(this);
  HINSTANCE   hInstResDll;
  HRSRC       hrscReg;
  HGLOBAL     hReg;
  DWORD       dwSize;
  LPSTR       szRegA;
  CTempBuffer<TCHAR, 1024> szReg;

  LPCTSTR lpszBSTRFileName = OLE2CT_EX(bstrFileName, _ATL_SAFE_ALLOCA_DEF_THRESHOLD);
#ifndef _UNICODE
  if (lpszBSTRFileName == NULL)
  {
    return E_OUTOFMEMORY;
  }
#endif // _UNICODE

  hInstResDll = LoadLibraryEx(lpszBSTRFileName, NULL, LOAD_LIBRARY_AS_DATAFILE);

  if (NULL == hInstResDll)
  {
    ATLTRACE(atlTraceRegistrar, 0, _T("Failed to LoadLibrary on %s\n"), bstrFileName);
    hr = AtlHresultFromLastError();
    goto ReturnHR;
  }

  hrscReg =FindResource((HMODULE)hInstResDll, szID, szType);

  if (NULL == hrscReg)
  {
    ATLTRACE(atlTraceRegistrar, 0, (HIWORD(szID) == NULL) ?
      _T("Failed to FindResource on ID:%d TYPE:%s\n") :
      _T("Failed to FindResource on ID:%s TYPE:%s\n"),
      szID, szType);
    hr = AtlHresultFromLastError();
    goto ReturnHR;
  }
  hReg = LoadResource((HMODULE)hInstResDll, hrscReg);

  if (NULL == hReg)
  {
    ATLTRACE(atlTraceRegistrar, 0, _T("Failed to LoadResource\n"));
    hr = AtlHresultFromLastError();
    goto ReturnHR;
  }

  dwSize = SizeofResource((HMODULE)hInstResDll, hrscReg);
  szRegA = (LPSTR)hReg;

  // Allocate extra space for NULL.
  if (dwSize + 1 < dwSize)
    return E_OUTOFMEMORY;
  ATLTRY(szReg.Allocate(dwSize + 1));
  if (szReg == NULL)
  {
    hr = E_OUTOFMEMORY;
    goto ReturnHR;
  }

#ifdef _UNICODE
  DWORD uniSize = ::MultiByteToWideChar(_AtlGetConversionACP(), 0, szRegA, dwSize, szReg, dwSize);
  if (uniSize == 0)
  {
    hr = AtlHresultFromLastError();
    goto ReturnHR;
  }
  // Append a NULL at the end.
  szReg[uniSize] = NULL;
#else
  Checked::memcpy_s(szReg, dwSize, szRegA, dwSize);
  // Append a NULL at the end.
     szReg[dwSize] = NULL;
#endif



  hr = parser.RegisterBuffer(szReg, bRegister);

ReturnHR:

  if (NULL != hInstResDll)
    FreeLibrary((HMODULE)hInstResDll);
  return hr;
}

inline HRESULT STDMETHODCALLTYPE RegObject::ResourceRegister(LPCOLESTR szFileName, UINT nID, LPCOLESTR szType)
{
  USES_CONVERSION_EX;

  LPCTSTR lpszT = OLE2CT_EX(szType, _ATL_SAFE_ALLOCA_DEF_THRESHOLD);
#ifndef _UNICODE
  if (lpszT == NULL)
  {
    return E_OUTOFMEMORY;
  }
#endif // _UNICODE

  return RegisterFromResource(szFileName, MAKEINTRESOURCE(nID), lpszT, TRUE);
}

inline HRESULT STDMETHODCALLTYPE RegObject::ResourceRegisterSz(LPCOLESTR szFileName, LPCOLESTR szID, LPCOLESTR szType)
{
  USES_CONVERSION_EX;
  if (szID == NULL || szType == NULL)
    return E_INVALIDARG;

  LPCTSTR lpszID = OLE2CT_EX(szID, _ATL_SAFE_ALLOCA_DEF_THRESHOLD);
  LPCTSTR lpszType = OLE2CT_EX(szType, _ATL_SAFE_ALLOCA_DEF_THRESHOLD);
#ifndef _UNICODE
  if (lpszID == NULL || lpszType==NULL)
  {
    return E_OUTOFMEMORY;
  }
#endif // _UNICODE
  return RegisterFromResource(szFileName, lpszID, lpszType, TRUE);
}

inline HRESULT STDMETHODCALLTYPE RegObject::ResourceUnregister(LPCOLESTR szFileName, UINT nID, LPCOLESTR szType)
{
  USES_CONVERSION_EX;

  LPCTSTR lpszT = OLE2CT_EX(szType, _ATL_SAFE_ALLOCA_DEF_THRESHOLD);
#ifndef _UNICODE
  if (lpszT == NULL)
  {
    return E_OUTOFMEMORY;
  }
#endif // _UNICODE
  return RegisterFromResource(szFileName, MAKEINTRESOURCE(nID), lpszT, FALSE);
}

inline HRESULT STDMETHODCALLTYPE RegObject::ResourceUnregisterSz(LPCOLESTR szFileName, LPCOLESTR szID, LPCOLESTR szType)
{
  USES_CONVERSION_EX;
  if (szID == NULL || szType == NULL)
    return E_INVALIDARG;

  LPCTSTR lpszID = OLE2CT_EX(szID, _ATL_SAFE_ALLOCA_DEF_THRESHOLD);
  LPCTSTR lpszType = OLE2CT_EX(szType, _ATL_SAFE_ALLOCA_DEF_THRESHOLD);
#ifndef _UNICODE
  if (lpszID == NULL || lpszType == NULL)
  {
    return E_OUTOFMEMORY;
  }
#endif // _UNICODE

  return RegisterFromResource(szFileName, lpszID, lpszType, FALSE);
}

inline HRESULT RegObject::RegisterWithString(LPCOLESTR bstrData, BOOL bRegister)
{
  USES_CONVERSION_EX;
  RegParser  parser(this);

  LPCTSTR szReg = OLE2CT_EX(bstrData, _ATL_SAFE_ALLOCA_DEF_THRESHOLD);
#ifndef _UNICODE
  if (szReg == NULL)
  {
    return E_OUTOFMEMORY;
  }
#endif // _UNICODE

  HRESULT hr = parser.RegisterBuffer((LPTSTR)szReg, bRegister);

  return hr;
}

inline HRESULT RegObject::ClearReplacements()
{
  m_csMap.Lock();
  HRESULT hr = m_RepMap.ClearReplacements();
  m_csMap.Unlock();
  return hr;
}


inline LPCOLESTR RegObject::StrFromMap(__in_z LPTSTR lpszKey)
{
  m_csMap.Lock();
  LPCOLESTR lpsz = m_RepMap.Lookup(lpszKey);
  if (lpsz == NULL) // not found!!
    ATLTRACE(atlTraceRegistrar, 0, _T("Map Entry not found\n"));
  m_csMap.Unlock();
  return lpsz;
}

inline HRESULT RegObject::CommonFileRegister(LPCOLESTR bstrFileName, BOOL bRegister)
{
  USES_CONVERSION_EX;

  RegParser  parser(this);

  LPCTSTR lpszBSTRFileName = OLE2CT_EX(bstrFileName, _ATL_SAFE_ALLOCA_DEF_THRESHOLD);
#ifndef _UNICODE
  if (lpszBSTRFileName == NULL)
  {
    return E_OUTOFMEMORY;
  }
#endif // _UNICODE

  HANDLE hFile = CreateFile(lpszBSTRFileName, GENERIC_READ, 0, NULL,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_READONLY,
                NULL);
  if (INVALID_HANDLE_VALUE == hFile)
  {
    ATLTRACE2(atlTraceRegistrar, 0, _T("Failed to CreateFile on %s\n"), lpszBSTRFileName);
    return AtlHresultFromLastError();
  }

  HRESULT hRes = S_OK;
  DWORD cbRead;
  DWORD cbFile = GetFileSize(hFile, NULL); // No HiOrder DWORD required

  CTempBuffer<char, 1024> szReg;
  // Extra space for NULL.
  ATLTRY(szReg.Allocate(cbFile + 1));
  if (szReg == NULL)
  {
    hRes = E_OUTOFMEMORY;
    goto ReturnHR;
  }

  if (ReadFile(hFile, szReg, cbFile, &cbRead, NULL) == 0)
  {
    ATLTRACE2(atlTraceRegistrar, 0, "Read Failed on file%s\n", lpszBSTRFileName);
    hRes =  AtlHresultFromLastError();
  }
  if (SUCCEEDED(hRes))
  {
    szReg[cbRead] = NULL;

#ifdef _UNICODE
    CTempBuffer<WCHAR, 1024> szConverted;
    ATLTRY(szConverted.Allocate(cbFile + 1));
    if (szConverted == NULL)
    {
      hRes =  E_OUTOFMEMORY;
      goto ReturnHR;

    }
    if (::MultiByteToWideChar(_AtlGetConversionACP(), 0, szReg, cbFile + 1, szConverted, cbFile + 1) == 0)
    {
      hRes = AtlHresultFromLastError();
      goto ReturnHR;
    }




#else
    LPTSTR szConverted = szReg;
#endif
    hRes = parser.RegisterBuffer(szConverted, bRegister);
  }
ReturnHR:
  CloseHandle(hFile);
  return hRes;
}

__declspec(selectany) const TCHAR* const RegParser::rgszNeverDelete[] =
{
  _T("AppID"),
  _T("CLSID"),
  _T("Component Categories"),
  _T("FileType"),
  _T("Interface"),
  _T("Hardware"),
  _T("Mime"),
  _T("SAM"),
  _T("SECURITY"),
  _T("SYSTEM"),
  _T("Software"),
  _T("TypeLib")
};

__declspec(selectany) const int RegParser::cbNeverDelete = sizeof(rgszNeverDelete) / sizeof(LPCTSTR*);


inline BOOL RegParser::VTFromRegType(LPCTSTR szValueType, VARTYPE& vt)
{
  struct typemap
  {
    LPCTSTR lpsz;
    VARTYPE vt;
  };
#pragma warning (push)
#pragma warning (disable : 4640)  // construction of local static object is not thread-safe

  static const typemap map[] = {
    {szStringVal, VT_BSTR},
    {multiszStringVal, VT_BSTR | VT_BYREF},
    {szDwordVal,  VT_UI4},
    {szBinaryVal, VT_UI1}
  };

#pragma warning (pop)

  for (int i=0;i<sizeof(map)/sizeof(typemap);i++)
  {
    if (!lstrcmpi(szValueType, map[i].lpsz))
    {
      vt = map[i].vt;
      return TRUE;
    }
  }

  return FALSE;

}

inline BYTE RegParser::ChToByte(const TCHAR ch)
{
  switch (ch)
  {
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
        return (BYTE) (ch - '0');
    case 'A':
    case 'B':
    case 'C':
    case 'D':
    case 'E':
    case 'F':
        return (BYTE) (10 + (ch - 'A'));
    case 'a':
    case 'b':
    case 'c':
    case 'd':
    case 'e':
    case 'f':
        return (BYTE) (10 + (ch - 'a'));
    default:
        ATLASSERT(FALSE);
        ATLTRACE(atlTraceRegistrar, 0, _T("Bogus value %c passed as binary Hex value\n"), ch);
        return 0;
  }
}

inline HKEY RegParser::HKeyFromString(__in_z LPTSTR szToken)
{
  struct keymap
  {
    LPCTSTR lpsz;
    HKEY hkey;
  };
  static const keymap map[] = {
    {_T("HKCR"), HKEY_CLASSES_ROOT},
    {_T("HKCU"), HKEY_CURRENT_USER},
    {_T("HKLM"), HKEY_LOCAL_MACHINE},
    {_T("HKU"),  HKEY_USERS},
    {_T("HKPD"), HKEY_PERFORMANCE_DATA},
    {_T("HKDD"), HKEY_DYN_DATA},
    {_T("HKCC"), HKEY_CURRENT_CONFIG},
    {_T("HKEY_CLASSES_ROOT"), HKEY_CLASSES_ROOT},
    {_T("HKEY_CURRENT_USER"), HKEY_CURRENT_USER},
    {_T("HKEY_LOCAL_MACHINE"), HKEY_LOCAL_MACHINE},
    {_T("HKEY_USERS"), HKEY_USERS},
    {_T("HKEY_PERFORMANCE_DATA"), HKEY_PERFORMANCE_DATA},
    {_T("HKEY_DYN_DATA"), HKEY_DYN_DATA},
    {_T("HKEY_CURRENT_CONFIG"), HKEY_CURRENT_CONFIG}
  };

  for (int i=0;i<sizeof(map)/sizeof(keymap);i++)
  {
    if (!lstrcmpi(szToken, map[i].lpsz))
      return map[i].hkey;
  }
  return NULL;
}

inline LPTSTR RegParser::StrChr(__in_z LPTSTR lpsz, __in TCHAR ch)
{
  LPTSTR p = NULL;

  if (lpsz == NULL)
    return NULL;

  while (*lpsz)
  {
    if (*lpsz == ch)
    {
      p = lpsz;
      break;
    }
    lpsz = CharNext(lpsz);
  }
  return p;
}

inline RegParser::RegParser(RegObject* pRegObj)
{
  m_pRegObj           = pRegObj;
  m_pchCur            = NULL;

  CString guid(m_pRegObj->StrFromMap(_T("CLSID")));
  if (guid.IsEmpty()) {
    guid = m_pRegObj->StrFromMap(_T("APPID"));
  }

  guid_prefix_ = _tcstoul(guid.Mid(1, 4), NULL, 16);

  ATLASSERT(guid_prefix_);
}

inline BOOL RegParser::IsSpace(TCHAR ch)
{
  switch (ch)
  {
    case _T(' '):
    case _T('\t'):
    case _T('\r'):
    case _T('\n'):
        return TRUE;
  }

  return FALSE;
}

inline void RegParser::SkipWhiteSpace()
{
  while(IsSpace(*m_pchCur))
    m_pchCur = CharNext(m_pchCur);
}

inline HRESULT RegParser::NextToken(__out_ecount_z(MAX_VALUE) LPTSTR szToken)
{
  SkipWhiteSpace();

  // NextToken cannot be called at EOS
  if (NULL == *m_pchCur)
    return GenerateError(E_ATL_UNEXPECTED_EOS);

  LPCTSTR szOrig = szToken;
  // handle quoted value / key
  if (chQuote == *m_pchCur)
  {
    m_pchCur = CharNext(m_pchCur);

    while (NULL != *m_pchCur && !EndOfVar())
    {
      if (chQuote == *m_pchCur) // If it is a quote that means we must skip it
        m_pchCur = CharNext(m_pchCur);

      LPTSTR pchPrev = m_pchCur;
      m_pchCur = CharNext(m_pchCur);

      INT_PTR nChars = m_pchCur - pchPrev;

      // Make sure we have room for nChars plus terminating NULL
      if ((szToken + nChars + 1) >= szOrig + MAX_VALUE)
        return GenerateError(E_ATL_VALUE_TOO_LARGE);

      for (int i = 0; i < (int)nChars; i++, szToken++, pchPrev++)
        *szToken = *pchPrev;
    }

    if (NULL == *m_pchCur)
    {
      ATLTRACE(atlTraceRegistrar, 0, _T("NextToken : Unexpected End of File\n"));
      return GenerateError(E_ATL_UNEXPECTED_EOS);
    }

    *szToken = NULL;
    m_pchCur = CharNext(m_pchCur);
  }

  else
  {
    // Handle non-quoted ie parse up till first "White Space"
    while (NULL != *m_pchCur && !IsSpace(*m_pchCur))
    {
      LPTSTR pchPrev = m_pchCur;
      m_pchCur = CharNext(m_pchCur);

      INT_PTR nChars = m_pchCur - pchPrev;

      // Make sure we have room for nChars plus terminating NULL
      if ((szToken + nChars + 1) >= szOrig + MAX_VALUE)
        return GenerateError(E_ATL_VALUE_TOO_LARGE);

      for (int i = 0; i < (int)nChars; i++, szToken++, pchPrev++)
        *szToken = *pchPrev;
    }

    *szToken = NULL;
  }
  return S_OK;
}

inline HRESULT RegParser::AddValue(__in CRegKey& rkParent, __in_z_opt LPCTSTR szValueName, __out_ecount_z(MAX_VALUE) LPTSTR szToken)
{
  HRESULT hr;

  TCHAR    szValue[MAX_VALUE];
  VARTYPE     vt = VT_EMPTY;
  LONG        lRes = ERROR_SUCCESS;
  UINT        nIDRes = 0;

  if (FAILED(hr = NextToken(szValue)))
    return hr;
  if (!VTFromRegType(szValue, vt))
  {
    ATLTRACE(atlTraceRegistrar, 0, _T("%s Type not supported\n"), szValue);
    return GenerateError(E_ATL_TYPE_NOT_SUPPORTED);
  }

  SkipWhiteSpace();
  if (FAILED(hr = NextToken(szValue)))
    return hr;

  switch (vt)
  {
  case VT_BSTR:
    {
      lRes = rkParent.SetStringValue(szValueName, szValue);
      ATLTRACE(atlTraceRegistrar, 2, _T("Setting Value %s at %s\n"), szValue, !szValueName ? _T("default") : szValueName);
      break;
    }
  case VT_BSTR | VT_BYREF:
    {
      ATLTRACE(atlTraceRegistrar, 2, _T("Setting Value %s at %s\n"), szValue, !szValueName ? _T("default") : szValueName);
      int nLen = lstrlen(szValue) + 2; //Allocate space for double null termination.
      CTempBuffer<TCHAR, 256> pszDestValue;
      //nLen should be >= the max size of the target buffer.
      ATLTRY(pszDestValue.Allocate(nLen));
      if (pszDestValue != NULL)
      {
        TCHAR* p = pszDestValue;
        TCHAR* q = szValue;
        nLen = 0;
        while (*q != _T('\0'))
        {
          TCHAR* r = CharNext(q);
          if (*q == _T('\\') && *r == _T('0'))
          {
            *p++ = NULL;
            q = CharNext(r);
          }
          else
          {
            *p = *q;
#ifndef _UNICODE
            if (IsDBCSLeadByte(*q))
            {
              p++;
              q++;
              //Protect from Lead byte followed by the zero terminator.May skip beyond the end of the string.
              if (*q == _T('\0')) { break; }
              *p = *q;
            }
#endif
            p++;
            q++;
          }
          nLen ++;
        }
         //Always terminate with 2 NULLs.
        *p = NULL;
        p++;
          *p = NULL;
        lRes = rkParent.SetMultiStringValue(szValueName, pszDestValue);
      }
      else
      {
        lRes = ERROR_OUTOFMEMORY;
      }
    }
    break;
  case VT_UI4:
    {
      ULONG ulVal;
      USES_CONVERSION_EX;

      LPOLESTR lpszV = T2OLE_EX(szValue, _ATL_SAFE_ALLOCA_DEF_THRESHOLD);
  #ifndef _UNICODE
      if(lpszV == NULL)
        return E_OUTOFMEMORY;
  #endif
      VarUI4FromStr(lpszV, 0, 0, &ulVal);

      lRes = rkParent.SetDWORDValue(szValueName, ulVal);
      ATLTRACE(atlTraceRegistrar, 2, _T("Setting Value %d at %s\n"), ulVal, !szValueName ? _T("default") : szValueName);
      break;
    }
  case VT_UI1:
    {
      int cbValue = lstrlen(szValue);
      if (cbValue & 0x00000001)
      {
        ATLTRACE(atlTraceRegistrar, 0, _T("Binary Data does not fall on BYTE boundries\n"));
        return E_FAIL;
      }
      int cbValDiv2 = cbValue/2;
      CTempBuffer<BYTE, 256> rgBinary;
      ATLTRY(rgBinary.Allocate(cbValDiv2));
      if (rgBinary == NULL)
        return E_FAIL;
      memset(rgBinary, 0, cbValDiv2);
      for (int irg = 0; irg < cbValue; irg++)
        rgBinary[(irg/2)] |= (ChToByte(szValue[irg])) << (4*(1 - (irg & 0x00000001)));
      lRes = RegSetValueEx(rkParent, szValueName, 0, REG_BINARY, rgBinary, cbValDiv2);
      break;
    }
  }

  if (ERROR_SUCCESS != lRes)
  {
    nIDRes = E_ATL_VALUE_SET_FAILED;
    return AtlHresultFromWin32(lRes);
  }

  if (FAILED(hr = NextToken(szToken)))
    return hr;

  return S_OK;
}

inline BOOL RegParser::CanForceRemoveKey(LPCTSTR szKey)
{
  for (int iNoDel = 0; iNoDel < cbNeverDelete; iNoDel++)
    if (!lstrcmpi(szKey, rgszNeverDelete[iNoDel]))
       return FALSE;                       // We cannot delete it

  return TRUE;
}

inline BOOL RegParser::HasSubKeys(HKEY hkey)
{
  DWORD       cbSubKeys = 0;

  if (RegQueryInfoKey(hkey, NULL, NULL, NULL,
                 &cbSubKeys, NULL, NULL,
                 NULL, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
  {
    ATLTRACE(atlTraceRegistrar, 0, _T("Should not be here!!\n"));
    ATLASSERT(FALSE);
    return FALSE;
  }

  return cbSubKeys > 0;
}

inline BOOL RegParser::HasValues(HKEY hkey)
{
  DWORD       cbValues = 0;

  LONG lResult = RegQueryInfoKey(hkey, NULL, NULL, NULL,
                  NULL, NULL, NULL,
                  &cbValues, NULL, NULL, NULL, NULL);
  if (ERROR_SUCCESS != lResult)
  {
    ATLTRACE(atlTraceRegistrar, 0, _T("RegQueryInfoKey Failed "));
    ATLASSERT(FALSE);
    return FALSE;
  }

  if (1 == cbValues)
  {
    DWORD cbMaxName= MAX_VALUE;
    TCHAR szValueName[MAX_VALUE];
    // Check to see if the Value is default or named
    lResult = RegEnumValue(hkey, 0, szValueName, &cbMaxName, NULL, NULL, NULL, NULL);
    if (ERROR_SUCCESS == lResult && (szValueName[0] != NULL))
      return TRUE; // Named Value means we have a value
    return FALSE;
  }

  return cbValues > 0; // More than 1 means we have a non-default value
}

inline HRESULT RegParser::SkipAssignment(__inout_ecount_z(MAX_VALUE) LPTSTR szToken)
{
  HRESULT hr;
  TCHAR szValue[MAX_VALUE];

  if (*szToken == chEquals)
  {
    if (FAILED(hr = NextToken(szToken)))
      return hr;
    // Skip assignment
    SkipWhiteSpace();
    if (FAILED(hr = NextToken(szValue)))
      return hr;
    if (FAILED(hr = NextToken(szToken)))
      return hr;
  }

  return S_OK;
}

inline HRESULT RegParser::PreProcessBuffer(__in_z LPTSTR lpszReg, __deref_out_z LPTSTR* ppszReg)
{
  ATLASSERT(lpszReg != NULL);
  ATLASSERT(ppszReg != NULL);

  if (lpszReg == NULL || ppszReg == NULL)
    return E_POINTER;

  *ppszReg = NULL;
  int nSize = lstrlen(lpszReg)*2;
  CParseBuffer pb(nSize);
  if (pb.p == NULL)
    return E_OUTOFMEMORY;
  m_pchCur = lpszReg;
  HRESULT hr = S_OK;

  while (*m_pchCur != NULL) // look for end
  {
    if (*m_pchCur == _T('%'))
    {
      m_pchCur = CharNext(m_pchCur);
      if (*m_pchCur == _T('%'))
      {
        if (!pb.AddChar(m_pchCur))
        {
          hr = E_OUTOFMEMORY;
          break;
        }
      }
      else
      {
        LPTSTR lpszNext = StrChr(m_pchCur, _T('%'));
        if (lpszNext == NULL)
        {
          ATLTRACE(atlTraceRegistrar, 0, _T("Error no closing %% found\n"));
          hr = GenerateError(E_ATL_UNEXPECTED_EOS);
          break;
        }
        if ((lpszNext-m_pchCur) > 31)
        {
          hr = E_FAIL;
          break;
        }
        int nLength = int(lpszNext - m_pchCur);
        TCHAR buf[32];
        Checked::tcsncpy_s(buf, _countof(buf), m_pchCur, nLength);
        LPCOLESTR lpszVar = m_pRegObj->StrFromMap(buf);
        if (lpszVar == NULL)
        {
          hr = GenerateError(E_ATL_NOT_IN_MAP);
          break;
        }
        if (!pb.AddString(lpszVar))
        {
          hr = E_OUTOFMEMORY;
          break;
        }

        while (m_pchCur != lpszNext)
          m_pchCur = CharNext(m_pchCur);
      }
    }
    else
    {
      if (!pb.AddChar(m_pchCur))
      {
        hr = E_OUTOFMEMORY;
        break;
      }
    }

    m_pchCur = CharNext(m_pchCur);
  }
  if (SUCCEEDED(hr))
    *ppszReg = pb.Detach();
  return hr;
}

inline HRESULT RegParser::RegisterBuffer(__in_z LPTSTR szBuffer, __in BOOL bRegister)
{
  TCHAR   szToken[MAX_VALUE];
  HRESULT hr = S_OK;

  LPTSTR szReg = NULL;
  hr = PreProcessBuffer(szBuffer, &szReg);
  if (FAILED(hr))
    return hr;

  ATLTRACE(atlTraceRegistrar, 0, _T("%s\n"), szReg);

  m_pchCur = szReg;

  // Preprocess szReg

  while (NULL != *m_pchCur)
  {
    if (FAILED(hr = NextToken(szToken)))
      break;
    HKEY hkBase;
    if ((hkBase = HKeyFromString(szToken)) == NULL)
    {
      ATLTRACE(atlTraceRegistrar, 0, _T("HKeyFromString failed on %s\n"), szToken);
      hr = GenerateError(E_ATL_BAD_HKEY);
      break;
    }

    if (FAILED(hr = NextToken(szToken)))
      break;

    if (chLeftBracket != *szToken)
    {
      ATLTRACE(atlTraceRegistrar, 0, _T("Syntax error, expecting a {, found a %s\n"), szToken);
      hr = GenerateError(E_ATL_MISSING_OPENKEY_TOKEN);
      break;
    }
    if (bRegister)
    {
      LPTSTR szRegAtRegister = m_pchCur;
      hr = RegisterSubkeys(szToken, hkBase, bRegister);
      if (FAILED(hr))
      {
        ATLTRACE(atlTraceRegistrar, 0, _T("Failed to register, cleaning up!\n"));
        m_pchCur = szRegAtRegister;
        RegisterSubkeys(szToken, hkBase, FALSE);
        break;
      }
    }
    else
    {
      if (FAILED(hr = RegisterSubkeys(szToken, hkBase, bRegister)))
        break;
    }

    SkipWhiteSpace();
  }
  CoTaskMemFree(szReg);
  return hr;
}

inline HRESULT RegParser::RegisterSubkeys(__out_ecount_z(MAX_VALUE) LPTSTR szToken, __in HKEY hkParent, __in BOOL bRegister, __in BOOL bRecover)
{
  CRegKey keyCur;
  LONG    lRes;
  TCHAR  szKey[_MAX_PATH];
  BOOL    bDelete = TRUE;
  BOOL    bInRecovery = bRecover;
  HRESULT hr = S_OK;

  ATLTRACE(atlTraceRegistrar, 2, _T("Num Els = %d\n"), cbNeverDelete);
  if (FAILED(hr = NextToken(szToken)))
    return hr;


  while (*szToken != chRightBracket) // Continue till we see a }
  {


    bDelete = TRUE;
    BOOL bTokenDelete = !lstrcmpi(szToken, szDelete);

    if (!lstrcmpi(szToken, szForceRemove) || bTokenDelete)
    {
      if (FAILED(hr = NextToken(szToken)))
        break;

      if (bRegister)
      {
        CRegKey rkForceRemove;

        if (StrChr(szToken, chDirSep) != NULL)
          return GenerateError(E_ATL_COMPOUND_KEY);

        if (CanForceRemoveKey(szToken))
        {
          rkForceRemove.Attach(hkParent);
          // Error not returned. We will overwrite the values any way.
          rkForceRemove.RecurseDeleteKey(szToken);
          rkForceRemove.Detach();
        }
        if (bTokenDelete)
        {
          if (FAILED(hr = NextToken(szToken)))
            break;
          if (FAILED(hr = SkipAssignment(szToken)))
            break;
          goto EndCheck;
        }
      }

    }

    if (!lstrcmpi(szToken, szNoRemove))
    {
      bDelete = FALSE;    // set even for register
      if (FAILED(hr = NextToken(szToken)))
        break;
    }

    if (!lstrcmpi(szToken, szValToken)) // need to add a value to hkParent
    {
      TCHAR  szValueName[_MAX_PATH];



      if (FAILED(hr = NextToken(szValueName)))
        break;
      if (FAILED(hr = NextToken(szToken)))
        break;

      if (*szToken != chEquals)
        return GenerateError(E_ATL_EXPECTING_EQUAL);

      if (bRegister)
      {
        CRegKey rk;

        rk.Attach(hkParent);
        hr = AddValue(rk, szValueName, szToken);
        rk.Detach();

        if (FAILED(hr))
          return hr;

        goto EndCheck;
      }
      else
      {
        if (!bRecover && bDelete)
        {
          ATLTRACE(atlTraceRegistrar, 1, _T("Deleting %s\n"), szValueName);
          // We have to open the key for write to be able to delete.
          CRegKey rkParent;
          lRes = rkParent.Open(hkParent, NULL, KEY_WRITE);
          if (lRes == ERROR_SUCCESS)
          {
            lRes = rkParent.DeleteValue(szValueName);
            if (lRes != ERROR_SUCCESS && lRes != ERROR_FILE_NOT_FOUND)
            {
              // Key not present is not an error
              hr = AtlHresultFromWin32(lRes);
              break;
            }
          }
          else
          {
            hr = AtlHresultFromWin32(lRes);
            break;
          }
        }
        if (FAILED(hr = SkipAssignment(szToken)))
          break;
        continue;  // can never have a subkey
      }
    }

    if (StrChr(szToken, chDirSep) != NULL)
      return GenerateError(E_ATL_COMPOUND_KEY);

    if (bRegister)
    {
      lRes = keyCur.Open(hkParent, szToken, KEY_READ | KEY_WRITE);
      if (ERROR_SUCCESS != lRes)
      {
        // Failed all access try read only
        lRes = keyCur.Open(hkParent, szToken, KEY_READ);
        if (ERROR_SUCCESS != lRes)
        {
          // Finally try creating it
          ATLTRACE(atlTraceRegistrar, 2, _T("Creating key %s\n"), szToken);
          lRes = keyCur.Create(hkParent, szToken, REG_NONE, REG_OPTION_NON_VOLATILE, KEY_READ | KEY_WRITE);
          if (lRes != ERROR_SUCCESS)
            return AtlHresultFromWin32(lRes);
        }
      }

      if (FAILED(hr = NextToken(szToken)))
        break;


      if (*szToken == chEquals)
      {
        if (FAILED(hr = AddValue(keyCur, NULL, szToken))) // NULL == default
          break;
      }
    }
    else //Unregister
    {
      if (!bRecover)
      {
         lRes = keyCur.Open(hkParent, szToken, KEY_READ);

      }
      else
        lRes = ERROR_FILE_NOT_FOUND;


      // Open failed set recovery mode
      if (lRes != ERROR_SUCCESS)
        bRecover = true;

      // TRACE out Key open status and if in recovery mode
#ifdef _DEBUG
      if (!bRecover)
        ATLTRACE(atlTraceRegistrar, 1, _T("Opened Key %s\n"), szToken);
      else
        ATLTRACE(atlTraceRegistrar, 0, _T("Ignoring Open key on %s : In Recovery mode\n"), szToken);
#endif //_DEBUG

      // Remember Subkey
      Checked::tcsncpy_s(szKey, _countof(szKey), szToken, _TRUNCATE);

      if (FAILED(hr = NextToken(szToken)))
        break;
      if (FAILED(hr = SkipAssignment(szToken)))
        break;

      if (*szToken == chLeftBracket && lstrlen(szToken) == 1)
      {
        hr = RegisterSubkeys(szToken, keyCur.m_hKey, bRegister, bRecover);
        // In recover mode ignore error
        if (FAILED(hr) && !bRecover)
          break;
        // Skip the }
        if (FAILED(hr = NextToken(szToken)))
          break;
      }

#ifdef _DEBUG
      if (bRecover != bInRecovery)
        ATLTRACE(atlTraceRegistrar, 0, _T("Ending Recovery Mode\n"));
#endif
      bRecover = bInRecovery;

      if (lRes == ERROR_FILE_NOT_FOUND)
        // Key already not present so not an error.
        continue;

      if (lRes != ERROR_SUCCESS)
      {
        // We are recovery mode continue on errors else break
        if (bRecover)
          continue;
        else
        {
          hr = AtlHresultFromWin32(lRes);
          break;
        }
      }

      // If in recovery mode
      if (bRecover && HasSubKeys(keyCur))
      {
        // See if the KEY is in the NeverDelete list and if so, don't
        if (CanForceRemoveKey(szKey) && bDelete)
        {
          ATLTRACE(atlTraceRegistrar, 0, _T("Deleting non-empty subkey %s by force\n"), szKey);
          // Error not returned since we are in recovery mode. The error that caused recovery mode is returned
          keyCur.RecurseDeleteKey(szKey);
        }
        continue;
      }

      BOOL bHasSubKeys=HasSubKeys(keyCur);
      lRes = keyCur.Close();
      if (lRes != ERROR_SUCCESS)
      return AtlHresultFromWin32(lRes);

      if (bDelete&& !bHasSubKeys)
      {
        ATLTRACE(atlTraceRegistrar, 0, _T("Deleting Key %s\n"), szKey);
        CRegKey rkParent;
        rkParent.Attach(hkParent);
        lRes = rkParent.DeleteSubKey(szKey);
        rkParent.Detach();
        if (lRes != ERROR_SUCCESS)
        {

          hr = AtlHresultFromWin32(lRes);
          break;
        }
      }




    }

EndCheck:

    if (bRegister)
    {
      if (*szToken == chLeftBracket && lstrlen(szToken) == 1)
      {
        if (FAILED(hr = RegisterSubkeys(szToken, keyCur.m_hKey, bRegister, FALSE)))
          break;
        if (FAILED(hr = NextToken(szToken)))
          break;
      }
    }
  }

  return hr;
}

};  //namespace omaha

#endif  //OMAHA_BASE_STATREGEX_H__

