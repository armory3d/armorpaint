// This software is provided 'as-is', without any express or implied
// warranty.  In no event will the authors be held liable for any damages
// arising from the use of this software.
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
// 1. The origin of this software must not be misrepresented; you must not
//    claim that you wrote the original software. If you use this software
//    in a product, an acknowledgment in the product documentation would be
//    appreciated but is not required.
// 2. Altered source versions must be plainly marked as such, and must not be
//    misrepresented as being the original software.
// 3. This notice may not be removed or altered from any source distribution.
// https://github.com/mlabbe/nativefiledialog

#include "nfd.h"
#include "../iron_global.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char g_errorstr[NFD_MAX_STRLEN] = {0};

/* public routines */

const char *NFD_GetError(void) {
	return g_errorstr;
}

size_t NFD_PathSet_GetCount(const nfdpathset_t *pathset) {
	assert(pathset);
	return pathset->count;
}

nfdchar_t *NFD_PathSet_GetPath(const nfdpathset_t *pathset, size_t num) {
	assert(pathset);
	assert(num < pathset->count);

	return pathset->buf + pathset->indices[num];
}

void NFD_PathSet_Free(nfdpathset_t *pathset) {
	assert(pathset);
	NFDi_Free(pathset->indices);
	NFDi_Free(pathset->buf);
}

/* internal routines */

void *NFDi_Malloc(size_t bytes) {
	void *ptr = malloc(bytes);
	if (!ptr)
		NFDi_SetError("NFDi_Malloc failed.");

	return ptr;
}

void NFDi_Free(void *ptr) {
	assert(ptr);
	free(ptr);
}

void NFDi_SetError(const char *msg) {
	int bTruncate = NFDi_SafeStrncpy(g_errorstr, msg, NFD_MAX_STRLEN);
	assert(!bTruncate);
	_NFD_UNUSED(bTruncate);
}

int NFDi_SafeStrncpy(char *dst, const char *src, size_t maxCopy) {
	size_t n = maxCopy;
	char  *d = dst;

	assert(src);
	assert(dst);

	while (n > 0 && *src != '\0') {
		*d++ = *src++;
		--n;
	}

	/* Truncation case -
	   terminate string and return true */
	if (n == 0) {
		dst[maxCopy - 1] = '\0';
		return 1;
	}

	/* No truncation.  Append a single NULL and return. */
	*d = '\0';
	return 0;
}

/* adapted from microutf8 */
int32_t NFDi_UTF8_Strlen(const nfdchar_t *str) {
	/* This function doesn't properly check validity of UTF-8 character
	sequence, it is supposed to use only with valid UTF-8 strings. */

	int32_t   character_count = 0;
	int32_t   i               = 0; /* Counter used to iterate over string. */
	nfdchar_t maybe_bom[4];

	/* If there is UTF-8 BOM ignore it. */
	if (strlen(str) > 2) {
		strncpy(maybe_bom, str, 3);
		maybe_bom[3] = 0;
		if (strcmp(maybe_bom, (nfdchar_t *)NFD_UTF8_BOM) == 0)
			i += 3;
	}

	while (str[i]) {
		if (str[i] >> 7 == 0) {
			/* If bit pattern begins with 0 we have ascii character. */
			++character_count;
		}
		else if (str[i] >> 6 == 3) {
			/* If bit pattern begins with 11 it is beginning of UTF-8 byte sequence. */
			++character_count;
		}
		else if (str[i] >> 6 == 2)
			; /* If bit pattern begins with 10 it is middle of utf-8 byte sequence. */
		else {
			/* In any other case this is not valid UTF-8. */
			return -1;
		}
		++i;
	}

	return character_count;
}

int NFDi_IsFilterSegmentChar(char ch) {
	return (ch == ',' || ch == ';' || ch == '\0');
}

#ifdef IRON_LINUX

#include "nfd_gtk.h"

const char INIT_FAIL_MSG[] = "gtk_init_check failed to initilaize GTK+";
const char LOAD_FAIL_MSG[] = "Failed to load GTK+ 3";

void iron_gtk_disable_setlocale();

static void AddTypeToFilterName(const char *typebuf, char *filterName, size_t bufsize) {
	const char SEP[] = ", ";

	size_t len = strlen(filterName);
	if (len != 0) {
		strncat(filterName, SEP, bufsize - len - 1);
		len += strlen(SEP);
	}

	strncat(filterName, typebuf, bufsize - len - 1);
}

static void AddFiltersToDialog(GtkWidget *dialog, const char *filterList) {
	GtkFileFilter *filter;
	char           typebuf[NFD_MAX_STRLEN]    = {0};
	const char    *p_filterList               = filterList;
	char          *p_typebuf                  = typebuf;
	char           filterName[NFD_MAX_STRLEN] = {0};

	if (!filterList || strlen(filterList) == 0)
		return;

	filter = gtk_file_filter_new();
	while (1) {

		if (NFDi_IsFilterSegmentChar(*p_filterList)) {
			char typebufWildcard[NFD_MAX_STRLEN];
			/* add another type to the filter */
			assert(strlen(typebuf) > 0);
			assert(strlen(typebuf) < NFD_MAX_STRLEN - 1);

			snprintf(typebufWildcard, NFD_MAX_STRLEN, "*.%s", typebuf);
			AddTypeToFilterName(typebuf, filterName, NFD_MAX_STRLEN);

			gtk_file_filter_add_pattern(filter, typebufWildcard);

			p_typebuf = typebuf;
			memset(typebuf, 0, sizeof(char) * NFD_MAX_STRLEN);
		}

		if (*p_filterList == ';' || *p_filterList == '\0') {
			/* end of filter -- add it to the dialog */

			gtk_file_filter_set_name(filter, filterName);
			gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

			filterName[0] = '\0';

			if (*p_filterList == '\0')
				break;

			filter = gtk_file_filter_new();
		}

		if (!NFDi_IsFilterSegmentChar(*p_filterList)) {
			*p_typebuf = *p_filterList;
			p_typebuf++;
		}

		p_filterList++;
	}

	/* always append a wildcard option to the end*/

	filter = gtk_file_filter_new();
	gtk_file_filter_set_name(filter, "*.*");
	gtk_file_filter_add_pattern(filter, "*");
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
}

static void SetDefaultPath(GtkWidget *dialog, const char *defaultPath) {
	if (!defaultPath || strlen(defaultPath) == 0)
		return;

	/* GTK+ manual recommends not specifically setting the default path.
	   We do it anyway in order to be consistent across platforms.

	   If consistency with the native OS is preferred, this is the line
	   to comment out. -ml */
	gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), defaultPath);
}

static nfdresult_t AllocPathSet(GSList *fileList, nfdpathset_t *pathSet) {
	size_t     bufSize = 0;
	GSList    *node;
	nfdchar_t *p_buf;
	size_t     count = 0;

	assert(fileList);
	assert(pathSet);

	pathSet->count = (size_t)g_slist_length(fileList);
	assert(pathSet->count > 0);

	pathSet->indices = NFDi_Malloc(sizeof(size_t) * pathSet->count);
	if (!pathSet->indices) {
		return NFD_ERROR;
	}

	/* count the total space needed for buf */
	for (node = fileList; node; node = node->next) {
		assert(node->data);
		bufSize += strlen((const gchar *)node->data) + 1;
	}

	pathSet->buf = NFDi_Malloc(sizeof(nfdchar_t) * bufSize);

	/* fill buf */
	p_buf = pathSet->buf;
	for (node = fileList; node; node = node->next) {
		nfdchar_t *path    = (nfdchar_t *)(node->data);
		size_t     byteLen = strlen(path) + 1;
		ptrdiff_t  index;

		memcpy(p_buf, path, byteLen);
		g_free(node->data);

		index = p_buf - pathSet->buf;
		assert(index >= 0);
		pathSet->indices[count] = (size_t)index;

		p_buf += byteLen;
		++count;
	}

	g_slist_free(fileList);

	return NFD_OKAY;
}

static void WaitForCleanup(void) {
	while (gtk_events_pending())
		gtk_main_iteration();
}

/* public */

nfdresult_t NFD_OpenDialog(const nfdchar_t *filterList, const nfdchar_t *defaultPath, nfdchar_t **outPath) {
	GtkWidget  *dialog;
	nfdresult_t result;

	if (!iron_gtk_load()) {
		NFDi_SetError(LOAD_FAIL_MSG);
		return NFD_ERROR;
	}
	iron_gtk_disable_setlocale();
	if (!gtk_init_check(NULL, NULL)) {
		NFDi_SetError(INIT_FAIL_MSG);
		return NFD_ERROR;
	}

	dialog = gtk_file_chooser_dialog_new("Open File", NULL, GTK_FILE_CHOOSER_ACTION_OPEN, "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, NULL);

	/* Build the filter list */
	AddFiltersToDialog(dialog, filterList);

	/* Set the default path */
	SetDefaultPath(dialog, defaultPath);

	result = NFD_CANCEL;
	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
		char *filename;

		filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));

		{
			size_t len = strlen(filename);
			*outPath   = NFDi_Malloc(len + 1);
			memcpy(*outPath, filename, len + 1);
			if (!*outPath) {
				g_free(filename);
				gtk_widget_destroy(dialog);
				return NFD_ERROR;
			}
		}
		g_free(filename);

		result = NFD_OKAY;
	}

	WaitForCleanup();
	gtk_widget_destroy(dialog);
	WaitForCleanup();

	return result;
}

nfdresult_t NFD_OpenDialogMultiple(const nfdchar_t *filterList, const nfdchar_t *defaultPath, nfdpathset_t *outPaths) {
	GtkWidget  *dialog;
	nfdresult_t result;

	if (!iron_gtk_load()) {
		NFDi_SetError(LOAD_FAIL_MSG);
		return NFD_ERROR;
	}
	iron_gtk_disable_setlocale();
	if (!gtk_init_check(NULL, NULL)) {
		NFDi_SetError(INIT_FAIL_MSG);
		return NFD_ERROR;
	}

	dialog = gtk_file_chooser_dialog_new("Open Files", NULL, GTK_FILE_CHOOSER_ACTION_OPEN, "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, NULL);
	gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(dialog), TRUE);

	/* Build the filter list */
	AddFiltersToDialog(dialog, filterList);

	/* Set the default path */
	SetDefaultPath(dialog, defaultPath);

	result = NFD_CANCEL;
	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
		GSList *fileList = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(dialog));
		if (AllocPathSet(fileList, outPaths) == NFD_ERROR) {
			gtk_widget_destroy(dialog);
			return NFD_ERROR;
		}

		result = NFD_OKAY;
	}

	WaitForCleanup();
	gtk_widget_destroy(dialog);
	WaitForCleanup();

	return result;
}

nfdresult_t NFD_SaveDialog(const nfdchar_t *filterList, const nfdchar_t *defaultPath, nfdchar_t **outPath) {
	GtkWidget  *dialog;
	nfdresult_t result;

	if (!iron_gtk_load()) {
		NFDi_SetError(LOAD_FAIL_MSG);
		return NFD_ERROR;
	}
	iron_gtk_disable_setlocale();
	if (!gtk_init_check(NULL, NULL)) {
		NFDi_SetError(INIT_FAIL_MSG);
		return NFD_ERROR;
	}

	dialog = gtk_file_chooser_dialog_new("Save File", NULL, GTK_FILE_CHOOSER_ACTION_SAVE, "_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, NULL);
	gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);

	/* Build the filter list */
	AddFiltersToDialog(dialog, filterList);

	/* Set the default path */
	SetDefaultPath(dialog, defaultPath);

	result = NFD_CANCEL;
	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
		char *filename;
		filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));

		{
			size_t len = strlen(filename);
			*outPath   = NFDi_Malloc(len + 1);
			memcpy(*outPath, filename, len + 1);
			if (!*outPath) {
				g_free(filename);
				gtk_widget_destroy(dialog);
				return NFD_ERROR;
			}
		}
		g_free(filename);

		result = NFD_OKAY;
	}

	WaitForCleanup();
	gtk_widget_destroy(dialog);
	WaitForCleanup();

	return result;
}

nfdresult_t NFD_PickFolder(const nfdchar_t *defaultPath, nfdchar_t **outPath) {
	GtkWidget  *dialog;
	nfdresult_t result;

	if (!iron_gtk_load()) {
		NFDi_SetError(LOAD_FAIL_MSG);
		return NFD_ERROR;
	}
	iron_gtk_disable_setlocale();
	if (!gtk_init_check(NULL, NULL)) {
		NFDi_SetError(INIT_FAIL_MSG);
		return NFD_ERROR;
	}

	dialog = gtk_file_chooser_dialog_new("Select folder", NULL, GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, "_Cancel", GTK_RESPONSE_CANCEL, "_Select",
	                                     GTK_RESPONSE_ACCEPT, NULL);
	gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);

	/* Set the default path */
	SetDefaultPath(dialog, defaultPath);

	result = NFD_CANCEL;
	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
		char *filename;
		filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));

		{
			size_t len = strlen(filename);
			*outPath   = NFDi_Malloc(len + 1);
			memcpy(*outPath, filename, len + 1);
			if (!*outPath) {
				g_free(filename);
				gtk_widget_destroy(dialog);
				return NFD_ERROR;
			}
		}
		g_free(filename);

		result = NFD_OKAY;
	}

	WaitForCleanup();
	gtk_widget_destroy(dialog);
	WaitForCleanup();

	return result;
}

#endif

#ifdef IRON_WINDOWS

#ifdef __MINGW32__
// Explicitly setting NTDDI version, this is necessary for the MinGW compiler
#define NTDDI_VERSION NTDDI_VISTA
#define _WIN32_WINNT  _WIN32_WINNT_VISTA
#endif

#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#include <stdlib.h>

/* only locally define UNICODE in this compilation unit */
#ifndef UNICODE
#define UNICODE
#endif

#include <assert.h>
#include <shobjidl.h>
#include <stdio.h>
#include <wchar.h>
#include <windows.h>

#define COM_INITFLAGS COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE

static BOOL COMIsInitialized(HRESULT coResult) {
	if (coResult == RPC_E_CHANGED_MODE) {
		// If COM was previously initialized with different init flags,
		// NFD still needs to operate. Eat this warning.
		return TRUE;
	}

	return SUCCEEDED(coResult);
}

static HRESULT COMInit(void) {
	return CoInitializeEx(NULL, COM_INITFLAGS);
}

static void COMUninit(HRESULT coResult) {
	// do not uninitialize if RPC_E_CHANGED_MODE occurred -- this
	// case does not refcount COM.
	if (SUCCEEDED(coResult))
		CoUninitialize();
}

// allocs the space in outPath -- call free()
static void CopyWCharToNFDChar(const wchar_t *inStr, nfdchar_t **outStr) {
	int inStrCharacterCount = (int)(wcslen(inStr));
	int bytesNeeded         = WideCharToMultiByte(CP_UTF8, 0, inStr, inStrCharacterCount, NULL, 0, NULL, NULL);
	assert(bytesNeeded);
	bytesNeeded += 1;

	*outStr = (nfdchar_t *)NFDi_Malloc(bytesNeeded);
	if (!*outStr)
		return;

	int bytesWritten = WideCharToMultiByte(CP_UTF8, 0, inStr, -1, *outStr, bytesNeeded, NULL, NULL);
	assert(bytesWritten);
	_NFD_UNUSED(bytesWritten);
}

/* includes NULL terminator byte in return */
static size_t GetUTF8ByteCountForWChar(const wchar_t *str) {
	size_t bytesNeeded = WideCharToMultiByte(CP_UTF8, 0, str, -1, NULL, 0, NULL, NULL);
	assert(bytesNeeded);
	return bytesNeeded + 1;
}

// write to outPtr -- no free() necessary.
static int CopyWCharToExistingNFDCharBuffer(const wchar_t *inStr, nfdchar_t *outPtr) {
	int bytesNeeded = (int)(GetUTF8ByteCountForWChar(inStr));

	/* invocation copies null term */
	int bytesWritten = WideCharToMultiByte(CP_UTF8, 0, inStr, -1, outPtr, bytesNeeded, NULL, 0);
	assert(bytesWritten);

	return bytesWritten;
}

// allocs the space in outStr -- call free()
static void CopyNFDCharToWChar(const nfdchar_t *inStr, wchar_t **outStr) {
	int inStrByteCount = (int)(strlen(inStr));
	int charsNeeded    = MultiByteToWideChar(CP_UTF8, 0, inStr, inStrByteCount, NULL, 0);
	assert(charsNeeded);
	assert(!*outStr);
	charsNeeded += 1; // terminator

	*outStr = (wchar_t *)NFDi_Malloc(charsNeeded * sizeof(wchar_t));
	if (!*outStr)
		return;

	int ret                    = MultiByteToWideChar(CP_UTF8, 0, inStr, inStrByteCount, *outStr, charsNeeded);
	(*outStr)[charsNeeded - 1] = '\0';

#ifdef _DEBUG
	int inStrCharacterCount = (int)(NFDi_UTF8_Strlen(inStr));
	assert(ret == inStrCharacterCount);
#else
	_NFD_UNUSED(ret);
#endif
}

/* ext is in format "jpg", no wildcards or separators */
static int AppendExtensionToSpecBuf(const char *ext, char *specBuf, size_t specBufLen) {
	const char SEP[] = ";";
	assert(specBufLen > strlen(ext) + 3);

	if (strlen(specBuf) > 0) {
		strncat(specBuf, SEP, specBufLen - strlen(specBuf) - 1);
		specBufLen += strlen(SEP);
	}

	char extWildcard[NFD_MAX_STRLEN];
	int  bytesWritten = sprintf_s(extWildcard, NFD_MAX_STRLEN, "*.%s", ext);
	assert(bytesWritten == (int)(strlen(ext) + 2));
	_NFD_UNUSED(bytesWritten);

	strncat(specBuf, extWildcard, specBufLen - strlen(specBuf) - 1);

	return NFD_OKAY;
}

static nfdresult_t AddFiltersToDialog(IFileDialog *fileOpenDialog, const char *filterList) {
	const wchar_t WILDCARD[] = L"*.*";

	if (!filterList || strlen(filterList) == 0)
		return NFD_OKAY;

	// Count rows to alloc
	UINT        filterCount = 1; /* guaranteed to have one filter on a correct, non-empty parse */
	const char *p_filterList;
	for (p_filterList = filterList; *p_filterList; ++p_filterList) {
		if (*p_filterList == ';')
			++filterCount;
	}

	assert(filterCount);
	if (!filterCount) {
		NFDi_SetError("Error parsing filters.");
		return NFD_ERROR;
	}

	/* filterCount plus 1 because we hardcode the *.* wildcard after the while loop */
	COMDLG_FILTERSPEC *specList = (COMDLG_FILTERSPEC *)NFDi_Malloc(sizeof(COMDLG_FILTERSPEC) * ((size_t)filterCount + 1));
	if (!specList) {
		return NFD_ERROR;
	}
	for (UINT i = 0; i < filterCount + 1; ++i) {
		specList[i].pszName = NULL;
		specList[i].pszSpec = NULL;
	}

	size_t specIdx                = 0;
	p_filterList                  = filterList;
	char  typebuf[NFD_MAX_STRLEN] = {0}; /* one per comma or semicolon */
	char *p_typebuf               = typebuf;

	char specbuf[NFD_MAX_STRLEN] = {0}; /* one per semicolon */

	while (1) {
		if (NFDi_IsFilterSegmentChar(*p_filterList)) {
			/* append a type to the specbuf (pending filter) */
			AppendExtensionToSpecBuf(typebuf, specbuf, NFD_MAX_STRLEN);

			p_typebuf = typebuf;
			memset(typebuf, 0, sizeof(char) * NFD_MAX_STRLEN);
		}

		if (*p_filterList == ';' || *p_filterList == '\0') {
			/* end of filter -- add it to specList */

			CopyNFDCharToWChar(specbuf, (wchar_t **)&specList[specIdx].pszName);
			CopyNFDCharToWChar(specbuf, (wchar_t **)&specList[specIdx].pszSpec);

			memset(specbuf, 0, sizeof(char) * NFD_MAX_STRLEN);
			++specIdx;
			if (specIdx == filterCount)
				break;
		}

		if (!NFDi_IsFilterSegmentChar(*p_filterList)) {
			*p_typebuf = *p_filterList;
			++p_typebuf;
		}

		++p_filterList;
	}

	/* Add wildcard */
	specList[specIdx].pszSpec = WILDCARD;
	specList[specIdx].pszName = WILDCARD;

	fileOpenDialog->lpVtbl->SetFileTypes(fileOpenDialog, filterCount + 1, specList);

	/* free speclist */
	for (size_t i = 0; i < filterCount; ++i) {
		NFDi_Free((void *)specList[i].pszSpec);
	}
	NFDi_Free(specList);

	return NFD_OKAY;
}

static nfdresult_t AllocPathSet(IShellItemArray *shellItems, nfdpathset_t *pathSet) {
	const char ERRORMSG[] = "Error allocating pathset.";

	assert(shellItems);
	assert(pathSet);

	// How many items in shellItems?
	DWORD   numShellItems;
	HRESULT result = shellItems->lpVtbl->GetCount(shellItems, &numShellItems);
	if (!SUCCEEDED(result)) {
		NFDi_SetError(ERRORMSG);
		return NFD_ERROR;
	}

	pathSet->count = (size_t)(numShellItems);
	assert(pathSet->count > 0);

	pathSet->indices = (size_t *)NFDi_Malloc(sizeof(size_t) * pathSet->count);
	if (!pathSet->indices) {
		return NFD_ERROR;
	}

	/* count the total bytes needed for buf */
	size_t bufSize = 0;
	for (DWORD i = 0; i < numShellItems; ++i) {
		IShellItem *shellItem;
		result = shellItems->lpVtbl->GetItemAt(shellItems, i, &shellItem);
		if (!SUCCEEDED(result)) {
			NFDi_SetError(ERRORMSG);
			return NFD_ERROR;
		}

		// Confirm SFGAO_FILESYSTEM is true for this shellitem, or ignore it.
		SFGAOF attribs;
		result = shellItem->lpVtbl->GetAttributes(shellItem, SFGAO_FILESYSTEM, &attribs);
		if (!SUCCEEDED(result)) {
			NFDi_SetError(ERRORMSG);
			return NFD_ERROR;
		}
		if (!(attribs & SFGAO_FILESYSTEM))
			continue;

		LPWSTR name;
		shellItem->lpVtbl->GetDisplayName(shellItem, SIGDN_FILESYSPATH, &name);

		// Calculate length of name with UTF-8 encoding
		bufSize += GetUTF8ByteCountForWChar(name);

		CoTaskMemFree(name);
	}

	assert(bufSize);

	pathSet->buf = (nfdchar_t *)NFDi_Malloc(sizeof(nfdchar_t) * bufSize);
	memset(pathSet->buf, 0, sizeof(nfdchar_t) * bufSize);

	/* fill buf */
	nfdchar_t *p_buf = pathSet->buf;
	for (DWORD i = 0; i < numShellItems; ++i) {
		IShellItem *shellItem;
		result = shellItems->lpVtbl->GetItemAt(shellItems, i, &shellItem);
		if (!SUCCEEDED(result)) {
			NFDi_SetError(ERRORMSG);
			return NFD_ERROR;
		}

		// Confirm SFGAO_FILESYSTEM is true for this shellitem, or ignore it.
		SFGAOF attribs;
		result = shellItem->lpVtbl->GetAttributes(shellItem, SFGAO_FILESYSTEM, &attribs);
		if (!SUCCEEDED(result)) {
			NFDi_SetError(ERRORMSG);
			return NFD_ERROR;
		}
		if (!(attribs & SFGAO_FILESYSTEM))
			continue;

		LPWSTR name;
		shellItem->lpVtbl->GetDisplayName(shellItem, SIGDN_FILESYSPATH, &name);

		int bytesWritten = CopyWCharToExistingNFDCharBuffer(name, p_buf);
		CoTaskMemFree(name);

		ptrdiff_t index = p_buf - pathSet->buf;
		assert(index >= 0);
		pathSet->indices[i] = (size_t)(index);

		p_buf += bytesWritten;
	}

	return NFD_OKAY;
}

static nfdresult_t SetDefaultPath(IFileDialog *dialog, const char *defaultPath) {
	if (!defaultPath || strlen(defaultPath) == 0)
		return NFD_OKAY;

	wchar_t *defaultPathW = {0};
	CopyNFDCharToWChar(defaultPath, &defaultPathW);

	IShellItem *folder;
	HRESULT     result = SHCreateItemFromParsingName(defaultPathW, NULL, &IID_IShellItem, (void **)&folder);

	// Valid non results.
	if (result == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) || result == HRESULT_FROM_WIN32(ERROR_INVALID_DRIVE)) {
		NFDi_Free(defaultPathW);
		return NFD_OKAY;
	}

	if (!SUCCEEDED(result)) {
		NFDi_SetError("Error creating ShellItem");
		NFDi_Free(defaultPathW);
		return NFD_ERROR;
	}

	// Could also call SetDefaultFolder(), but this guarantees defaultPath -- more consistency across API.
	dialog->lpVtbl->SetFolder(dialog, folder);

	NFDi_Free(defaultPathW);
	folder->lpVtbl->Release(folder);

	return NFD_OKAY;
}

/* public */

nfdresult_t NFD_OpenDialog(const nfdchar_t *filterList, const nfdchar_t *defaultPath, nfdchar_t **outPath) {
	nfdresult_t nfdResult = NFD_ERROR;

	HRESULT coResult = COMInit();
	if (!COMIsInitialized(coResult)) {
		NFDi_SetError("Could not initialize COM.");
		return nfdResult;
	}

	// Create dialog
	IFileOpenDialog *fileOpenDialog;
	HRESULT          result = CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_ALL, &IID_IFileOpenDialog, (void **)(&fileOpenDialog));

	if (!SUCCEEDED(result)) {
		NFDi_SetError("Could not create dialog.");
		goto end;
	}

	// Build the filter list
	if (!AddFiltersToDialog(fileOpenDialog, filterList)) {
		goto end;
	}

	// Set the default path
	if (!SetDefaultPath(fileOpenDialog, defaultPath)) {
		goto end;
	}

	// Show the dialog.
	result = fileOpenDialog->lpVtbl->Show(fileOpenDialog, NULL);
	if (SUCCEEDED(result)) {
		// Get the file name
		IShellItem *shellItem;
		result = fileOpenDialog->lpVtbl->GetResult(fileOpenDialog, &shellItem);
		if (!SUCCEEDED(result)) {
			NFDi_SetError("Could not get shell item from dialog.");
			goto end;
		}
		wchar_t *filePath;
		result = shellItem->lpVtbl->GetDisplayName(shellItem, SIGDN_FILESYSPATH, &filePath);
		if (!SUCCEEDED(result)) {
			NFDi_SetError("Could not get file path for selected.");
			shellItem->lpVtbl->Release(shellItem);
			goto end;
		}

		CopyWCharToNFDChar(filePath, outPath);
		CoTaskMemFree(filePath);
		if (!*outPath) {
			/* error is malloc-based, error message would be redundant */
			shellItem->lpVtbl->Release(shellItem);
			goto end;
		}

		nfdResult = NFD_OKAY;
		shellItem->lpVtbl->Release(shellItem);
	}
	else if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
		nfdResult = NFD_CANCEL;
	}
	else {
		NFDi_SetError("File dialog box show failed.");
		nfdResult = NFD_ERROR;
	}

end:
	if (fileOpenDialog)
		fileOpenDialog->lpVtbl->Release(fileOpenDialog);

	COMUninit(coResult);

	return nfdResult;
}

nfdresult_t NFD_OpenDialogMultiple(const nfdchar_t *filterList, const nfdchar_t *defaultPath, nfdpathset_t *outPaths) {
	nfdresult_t nfdResult = NFD_ERROR;

	HRESULT coResult = COMInit();
	if (!COMIsInitialized(coResult)) {
		NFDi_SetError("Could not initialize COM.");
		return nfdResult;
	}

	// Create dialog
	IFileOpenDialog *fileOpenDialog;
	HRESULT          result = CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_ALL, &IID_IFileOpenDialog, (void **)(&fileOpenDialog));

	if (!SUCCEEDED(result)) {
		fileOpenDialog = NULL;
		NFDi_SetError("Could not create dialog.");
		goto end;
	}

	// Build the filter list
	if (!AddFiltersToDialog(fileOpenDialog, filterList)) {
		goto end;
	}

	// Set the default path
	if (!SetDefaultPath(fileOpenDialog, defaultPath)) {
		goto end;
	}

	// Set a flag for multiple options
	DWORD dwFlags;
	result = fileOpenDialog->lpVtbl->GetOptions(fileOpenDialog, &dwFlags);
	if (!SUCCEEDED(result)) {
		NFDi_SetError("Could not get options.");
		goto end;
	}
	result = fileOpenDialog->lpVtbl->SetOptions(fileOpenDialog, dwFlags | FOS_ALLOWMULTISELECT);
	if (!SUCCEEDED(result)) {
		NFDi_SetError("Could not set options.");
		goto end;
	}

	// Show the dialog.
	result = fileOpenDialog->lpVtbl->Show(fileOpenDialog, NULL);
	if (SUCCEEDED(result)) {
		IShellItemArray *shellItems;
		result = fileOpenDialog->lpVtbl->GetResults(fileOpenDialog, &shellItems);
		if (!SUCCEEDED(result)) {
			NFDi_SetError("Could not get shell items.");
			goto end;
		}

		if (AllocPathSet(shellItems, outPaths) == NFD_ERROR) {
			shellItems->lpVtbl->Release(shellItems);
			goto end;
		}

		shellItems->lpVtbl->Release(shellItems);
		nfdResult = NFD_OKAY;
	}
	else if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
		nfdResult = NFD_CANCEL;
	}
	else {
		NFDi_SetError("File dialog box show failed.");
		nfdResult = NFD_ERROR;
	}

end:
	if (fileOpenDialog)
		fileOpenDialog->lpVtbl->Release(fileOpenDialog);

	COMUninit(coResult);

	return nfdResult;
}

nfdresult_t NFD_SaveDialog(const nfdchar_t *filterList, const nfdchar_t *defaultPath, nfdchar_t **outPath) {
	nfdresult_t nfdResult = NFD_ERROR;

	HRESULT coResult = COMInit();
	if (!COMIsInitialized(coResult)) {
		NFDi_SetError("Could not initialize COM.");
		return nfdResult;
	}

	// Create dialog
	IFileSaveDialog *fileSaveDialog;
	HRESULT          result = CoCreateInstance(&CLSID_FileSaveDialog, NULL, CLSCTX_ALL, &IID_IFileSaveDialog, (void **)(&fileSaveDialog));

	if (!SUCCEEDED(result)) {
		fileSaveDialog = NULL;
		NFDi_SetError("Could not create dialog.");
		goto end;
	}

	// Build the filter list
	if (!AddFiltersToDialog(fileSaveDialog, filterList)) {
		goto end;
	}

	// Set the default path
	if (!SetDefaultPath(fileSaveDialog, defaultPath)) {
		goto end;
	}

	// Show the dialog.
	result = fileSaveDialog->lpVtbl->Show(fileSaveDialog, NULL);
	if (SUCCEEDED(result)) {
		// Get the file name
		IShellItem *shellItem;
		result = fileSaveDialog->lpVtbl->GetResult(fileSaveDialog, &shellItem);
		if (!SUCCEEDED(result)) {
			NFDi_SetError("Could not get shell item from dialog.");
			goto end;
		}
		wchar_t *filePath;
		result = shellItem->lpVtbl->GetDisplayName(shellItem, SIGDN_FILESYSPATH, &filePath);
		if (!SUCCEEDED(result)) {
			shellItem->lpVtbl->Release(shellItem);
			NFDi_SetError("Could not get file path for selected.");
			goto end;
		}

		CopyWCharToNFDChar(filePath, outPath);
		CoTaskMemFree(filePath);
		if (!*outPath) {
			/* error is malloc-based, error message would be redundant */
			shellItem->lpVtbl->Release(shellItem);
			goto end;
		}

		nfdResult = NFD_OKAY;
		shellItem->lpVtbl->Release(shellItem);
	}
	else if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
		nfdResult = NFD_CANCEL;
	}
	else {
		NFDi_SetError("File dialog box show failed.");
		nfdResult = NFD_ERROR;
	}

end:
	if (fileSaveDialog)
		fileSaveDialog->lpVtbl->Release(fileSaveDialog);

	COMUninit(coResult);

	return nfdResult;
}

nfdresult_t NFD_PickFolder(const nfdchar_t *defaultPath, nfdchar_t **outPath) {
	nfdresult_t nfdResult = NFD_ERROR;
	DWORD       dwOptions = 0;

	HRESULT coResult = COMInit();
	if (!COMIsInitialized(coResult)) {
		NFDi_SetError("CoInitializeEx failed.");
		return nfdResult;
	}

	// Create dialog
	IFileOpenDialog *fileDialog;
	HRESULT          result = CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_ALL, &IID_IFileOpenDialog, (void **)&fileDialog);
	if (!SUCCEEDED(result)) {
		NFDi_SetError("CoCreateInstance for CLSID_FileOpenDialog failed.");
		goto end;
	}

	// Set the default path
	if (SetDefaultPath(fileDialog, defaultPath) != NFD_OKAY) {
		NFDi_SetError("SetDefaultPath failed.");
		goto end;
	}

	// Get the dialogs options
	if (!SUCCEEDED(fileDialog->lpVtbl->GetOptions(fileDialog, &dwOptions))) {
		NFDi_SetError("GetOptions for IFileDialog failed.");
		goto end;
	}

	// Add in FOS_PICKFOLDERS which hides files and only allows selection of folders
	if (!SUCCEEDED(fileDialog->lpVtbl->SetOptions(fileDialog, dwOptions | FOS_PICKFOLDERS))) {
		NFDi_SetError("SetOptions for IFileDialog failed.");
		goto end;
	}

	// Show the dialog to the user
	result = fileDialog->lpVtbl->Show(fileDialog, NULL);
	if (SUCCEEDED(result)) {
		// Get the folder name
		IShellItem *shellItem;

		result = fileDialog->lpVtbl->GetResult(fileDialog, &shellItem);
		if (!SUCCEEDED(result)) {
			NFDi_SetError("Could not get file path for selected.");
			shellItem->lpVtbl->Release(shellItem);
			goto end;
		}

		wchar_t *path = NULL;
		result        = shellItem->lpVtbl->GetDisplayName(shellItem, SIGDN_DESKTOPABSOLUTEPARSING, &path);
		if (!SUCCEEDED(result)) {
			NFDi_SetError("GetDisplayName for IShellItem failed.");
			shellItem->lpVtbl->Release(shellItem);
			goto end;
		}

		CopyWCharToNFDChar(path, outPath);
		CoTaskMemFree(path);
		if (!*outPath) {
			shellItem->lpVtbl->Release(shellItem);
			goto end;
		}

		nfdResult = NFD_OKAY;
		shellItem->lpVtbl->Release(shellItem);
	}
	else if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
		nfdResult = NFD_CANCEL;
	}
	else {
		NFDi_SetError("Show for IFileDialog failed.");
		nfdResult = NFD_ERROR;
	}

end:

	if (fileDialog)
		fileDialog->lpVtbl->Release(fileDialog);

	COMUninit(coResult);

	return nfdResult;
}

#endif
