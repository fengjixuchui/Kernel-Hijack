#include "Utilities\Utils.h"
#include <winternl.h>
#include <iostream>

#define SERVICE_REG_SUBKEY "System\\CurrentControlSet\\Services\\"
#define REGISTRY_PATH_PREFIX "\\Registry\\Machine\\"

NTSTATUS(NTAPI* NtLoadDriver)(_In_ PUNICODE_STRING DriverServiceName);
NTSTATUS(NTAPI* NtUnloadDriver)(_In_ PUNICODE_STRING DriverServiceName);
PVOID(NTAPI* MmGetSystemRoutineAddress)(_In_ PUNICODE_STRING);


Utils* g_pUtils = new Utils();

Utils::Utils()
{
}


Utils::~Utils()
{
}



PVOID Utils::GetSystemRoutine(PVOID pMmGetSystemRoutineAddress, const wchar_t* RoutineName)
{
	if (RoutineName == nullptr || pMmGetSystemRoutineAddress == nullptr)
		return 0;

	MmGetSystemRoutineAddress = (decltype(MmGetSystemRoutineAddress))pMmGetSystemRoutineAddress;
	UNICODE_STRING usRoutine;
	RtlInitUnicodeString(&usRoutine, RoutineName);
	return MmGetSystemRoutineAddress(&usRoutine);
}


/* Elevates Process Privileges To Desired Privilege */
BOOLEAN Utils::EnablePrivilege(const char* lpPrivilegeName)
{
	TOKEN_PRIVILEGES Privilege;
	HANDLE hToken;
	DWORD dwErrorCode;

	Privilege.PrivilegeCount = 1;
	Privilege.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
	if (!LookupPrivilegeValueA(NULL, lpPrivilegeName,
		&Privilege.Privileges[0].Luid))
		return GetLastError();

	if (!OpenProcessToken(GetCurrentProcess(),
		TOKEN_ADJUST_PRIVILEGES, &hToken))
		return GetLastError();

	if (!AdjustTokenPrivileges(hToken, FALSE, &Privilege, sizeof(Privilege),
		NULL, NULL)) {
		dwErrorCode = GetLastError();
		CloseHandle(hToken);
		return dwErrorCode;
	}

	CloseHandle(hToken);
	return TRUE;
}


/* Registers a Service To The Registry */
/* Includes a ImagePath, Type, ErrorControl and Start, SubKeys */
BOOLEAN Utils::RegisterService(std::string ServicePath, std::string *ServiceRegKey)
{
	HKEY hkResult;
	DWORD dwDispositon;
	DWORD dwServiceType = 1;
	DWORD dwServiceErrorControl = 1;
	DWORD dwServiceStart = 3;
	//DWORD dwProcType = 1;
	LPCSTR lpValueName = "ImagePath";
	LPCSTR lpType = "Type";
	LPCSTR lpErrorControl = "ErrorControl";
	LPCSTR lpValueStart = "Start";
	//LPCSTR lpDisplayName = "DisplayName";
	//LPCSTR lpProcType = "WOW64";

	size_t OffsetToServiceName = ServicePath.find_last_of('\\');
	std::string ServiceName = ServicePath.substr(OffsetToServiceName + 1);	// Get Service Name With ".sys" suffix

	std::string KeyName = ServiceName.substr(0, ServiceName.find_first_of('.'));		// Get KeyName, (ServiceName without suffix ".sys")
	std::string SubKey = SERVICE_REG_SUBKEY + KeyName;
	*ServiceRegKey = REGISTRY_PATH_PREFIX + SubKey;				// ServiceRegKey Needed To Load/Unload Driver With Native Functions

	LSTATUS Status = RegOpenKeyExA(HKEY_LOCAL_MACHINE, SubKey.c_str(), 0, KEY_ALL_ACCESS, &hkResult);		// Try To Open Registry Key (Checking if it exists)
	if (!Status)																							// If it exists,
		RegDeleteKeyExA(HKEY_LOCAL_MACHINE, SubKey.c_str(), KEY_WOW64_64KEY, 0);							// then remove it for creating a new with correct SubKeys.

	Status = RegCreateKeyExA(HKEY_LOCAL_MACHINE, SubKey.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS, NULL, &hkResult, &dwDispositon);		// Create SubKey.
	if (Status)
		return false;

	if (Status = RegSetValueExA(hkResult, lpValueName, 0, REG_EXPAND_SZ, (const BYTE*)std::string("\\??\\" + ServicePath).c_str(), ServicePath.size() + 4))		// Include prefix "\\??\\" for correct ImagePath
	{
		/* Error already caught */
	}
	else if (Status = RegSetValueExA(hkResult, lpType, 0, REG_DWORD, (const BYTE*)&dwServiceType, sizeof(DWORD)))	// Set Type Key
	{
		/* Error already caught */
	}
	else if (Status = RegSetValueExA(hkResult, lpErrorControl, 0, REG_DWORD, (const BYTE*)&dwServiceErrorControl, sizeof(DWORD)))	// Set ErrorControl Key
	{
		/* Error already caught */
	}
	else if (Status = RegSetValueExA(hkResult, lpValueStart, 0, REG_DWORD, (const BYTE*)&dwServiceStart, sizeof(DWORD)))	// Set Start Key
	{
		/* Error already caught */
	}
	/*else if (Status = RegSetValueExA(hkResult, lpDisplayName, 0, REG_SZ, (const BYTE*)KeyName.c_str(), KeyName.size()))
	{

	}
	else if (Status = RegSetValueExA(hkResult, lpProcType, 0, REG_DWORD, (const BYTE*)&dwProcType, sizeof(DWORD)))
	{
	}*/

	// ErrorHandling:
	RegCloseKey(hkResult);
	return Status == 0;
}


/* Initializes Native Functions For Loading And Unloading Drivers */
BOOLEAN Utils::InitNativeFuncs()
{
	HMODULE hNtdll = GetModuleHandle("ntdll.dll");
	if (!hNtdll)
		return FALSE;

	/* Look up desired functions */
	NtLoadDriver = (decltype(NtLoadDriver))GetProcAddress(hNtdll, "NtLoadDriver");
	NtUnloadDriver = (decltype(NtLoadDriver))GetProcAddress(hNtdll, "NtUnloadDriver");

	if (!NtLoadDriver || !NtUnloadDriver)
		return FALSE;

	m_bIsNativeInitialized = TRUE;		// Set their Initialization to TRUE for no Reinitialization
	return TRUE;
}


/* Loads A Driver Specified With The Service Registry Key */
NTSTATUS Utils::LoadDriver(std::string ServiceRegKey)
{
	std::cout << "Loading: " << ServiceRegKey.substr(ServiceRegKey.find_last_of('\\') + 1).c_str() << ".sys\n";
	UNICODE_STRING usKey{ 0 };
	std::wstring ServiceRegKeyW(ServiceRegKey.begin(), ServiceRegKey.end());

	if (!m_bIsNativeInitialized)
		if (!InitNativeFuncs())		// Initialize if it has not been initialized before
			return FALSE;

	RtlInitUnicodeString(&usKey, ServiceRegKeyW.c_str());
	return NtLoadDriver(&usKey);
}


/* Unloads A Driver Specified With The Service Registry Key */
NTSTATUS Utils::UnloadDriver(std::string ServiceRegKey)
{
	std::cout << "Unloading: " << ServiceRegKey.substr(ServiceRegKey.find_last_of('\\') + 1).c_str() << ".sys\n";
	UNICODE_STRING usKey{ 0 };
	std::wstring ServiceRegKeyW(ServiceRegKey.begin(), ServiceRegKey.end());

	if (!m_bIsNativeInitialized)
		if (!InitNativeFuncs())
			return FALSE;

	RtlInitUnicodeString(&usKey, ServiceRegKeyW.c_str());
	return NtUnloadDriver(&usKey);
}


int Utils::isAscii(int c)
{
	return((c >= 'A' && c <= 'z') || (c >= '0' && c <= '9') || c == 0x20 || c == '@' || c == '_' || c == '?');
}


int Utils::isPrintable(uint32_t uint32)
{
	if ((isAscii((uint32 >> 24) & 0xFF)) && (isAscii((uint32 >> 16) & 0xFF)) && (isAscii((uint32 >> 8) & 0xFF)) &&
		(isAscii((uint32) & 0xFF)))
		return true;
	else
		return false;
}


/* Converts a character array to lower characters */
char* Utils::ToLower(char* szText)
{
	char* T = (char*)malloc(MAX_PATH);		// Allocate memory for new character array
	ZeroMemory(T, MAX_PATH);
	int i = 0;
	while (szText[i])
	{
		T[i] = tolower(szText[i]);
		++i;
	}
	return T;
}