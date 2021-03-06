#include "Memory\Proc.h"
#include <Windows.h>
#include <string>
#include <TlHelp32.h>
#include <SubAuth.h>
#include "Memory\MemIter.h"
#include "Speedfan\Speedfan.h"
#include "Utilities\Utils.h"






Proc* g_pProc = new Proc();


/*
	"Attaches" to process by getting dir table
*/
BOOLEAN Proc::OnSetup(std::string ProcessName)
{
	m_ProcessName = ProcessName;

	//if (!GetProcessId())
	//	return false;

	if (!g_pUtils->EnablePrivilege("SeLoadDriverPrivilege"))	// Set load driver privileges
		return false;	

	if (!g_pSpdfan->OnSetup())		// Load Speedfan driver
		return false;
		
	
	// Set up functions
	auto ReadPhysicalAddress = [=](uint64_t physAddress, DWORD Size, LPVOID Return) { return g_pSpdfan->ReadPhysicalAddress(physAddress, Size, Return); };	
	auto Callback = [=](PVOID VaBlock, PVOID PhysBlock, ULONG BlockSize, PVOID Context) { return this->Callback(VaBlock, PhysBlock, BlockSize, Context); };

	// Pass functions to memory iterator
	if (!g_pMemIter->OnSetup(Callback, ReadPhysicalAddress))
		return false;

	//m_DirectoryTable = 0x119500000;

	// Iterate memory for Pooltag "Proc"
	if (!g_pMemIter->IterateMemory("Proc", (PVOID)m_ProcessName.c_str()))
		return false;
	
	return m_DirectoryTable != 0;
}


Proc::~Proc()
{
}




/*
	Memory iterator callback
*/
BOOLEAN Proc::Callback(PVOID VaBlock, PVOID PhysBlock, ULONG BlockSize, PVOID Context)
{
	const char* ProcName = (const char*)Context;
	
	//auto pObjectHeader = (POBJECT_HEADER)((uint8_t*)VaBlock + 0x30);
	auto pEprocess = (uint8_t*)((uint8_t*)VaBlock + 0x80);
	auto pid = *(uint64_t*)(pEprocess + 0x2E0);

	printf("PID: %d\tName: %s\n", pid, pEprocess + 0x450);

	if (!strcmp((char*)pEprocess + 0x450, ProcName))
	{
		m_PhysEprocess = (uint8_t*)PhysBlock + 0x80;
		m_DirectoryTable = *(uint64_t*)(pEprocess + 0x28);
		m_VaPEB = *(uint64_t*)(pEprocess + 0x3F8);
		return true;
	}

	return false;
}





/*
	Get process id from name
*/
BOOLEAN Proc::GetProcessId()
{
	HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);
	PROCESSENTRY32 entry{ sizeof(PROCESSENTRY32) };
	if (hSnap != INVALID_HANDLE_VALUE)
	{
		if (Process32First(hSnap, &entry))
		{
			do
			{
				if (!m_ProcessName.compare(entry.szExeFile))
				{
					m_ProcessId = entry.th32ProcessID;
					CloseHandle(hSnap);
					return true;
				}
			} while (Process32Next(hSnap, &entry));
		}
		CloseHandle(hSnap);
	}
	return false;
}



BOOLEAN Proc::ReadProcessMemory(PVOID Address, DWORD Size, PVOID Dst)
{
	if (!Address || !Size || !Dst || !m_DirectoryTable)
		return false;
	uint64_t PhysicalAddress = TranslateVirtualAddress(m_DirectoryTable, Address);
	return g_pSpdfan->ReadPhysicalAddress(PhysicalAddress, Size, Dst);
}


BOOLEAN Proc::WriteProcessMemory(PVOID Address, DWORD Size, PVOID Src)
{
	if (!Address || !Size || !Src || !m_DirectoryTable)
		return false;
	uint64_t PhysicalAddress = TranslateVirtualAddress(m_DirectoryTable, Address);
	return g_pSpdfan->WritePhysicalAddress(PhysicalAddress, Size, Src);
}


/* Translating Virtual Address To Physical Address, Using a Table Base */
uint64_t Proc::TranslateVirtualAddress(uint64_t directoryTableBase, LPVOID virtualAddress)
{
	auto va = (uint64_t)virtualAddress;

	auto PML4 = (USHORT)((va >> 39) & 0x1FF); //<! PML4 Entry Index
	auto DirectoryPtr = (USHORT)((va >> 30) & 0x1FF); //<! Page-Directory-Pointer Table Index
	auto Directory = (USHORT)((va >> 21) & 0x1FF); //<! Page Directory Table Index
	auto Table = (USHORT)((va >> 12) & 0x1FF); //<! Page Table Index

											   // 
											   // Read the PML4 Entry. DirectoryTableBase has the base address of the table.
											   // It can be read from the CR3 register or from the kernel process object.
											   // 
	auto PML4E = g_pSpdfan->ReadPhysicalAddress<uint64_t>(directoryTableBase + PML4 * sizeof(ULONGLONG));

	if (PML4E == 0)
		return 0;

	// 
	// The PML4E that we read is the base address of the next table on the chain,
	// the Page-Directory-Pointer Table.
	// 
	auto PDPTE = g_pSpdfan->ReadPhysicalAddress<uint64_t>((PML4E & 0xFFFFFFFFFF000) + DirectoryPtr * sizeof(ULONGLONG));

	if (PDPTE == 0)
		return 0;

	//Check the PS bit
	if ((PDPTE & (1 << 7)) != 0) {
		// If the PDPTE�s PS flag is 1, the PDPTE maps a 1-GByte page. The
		// final physical address is computed as follows:
		// � Bits 51:30 are from the PDPTE.
		// � Bits 29:0 are from the original va address.
		return (PDPTE & 0xFFFFFC0000000) + (va & 0x3FFFFFFF);
	}

	//
	// PS bit was 0. That means that the PDPTE references the next table
	// on the chain, the Page Directory Table. Read it.
	// 
	auto PDE = g_pSpdfan->ReadPhysicalAddress<uint64_t>((PDPTE & 0xFFFFFFFFFF000) + Directory * sizeof(ULONGLONG));

	if (PDE == 0)
		return 0;

	if ((PDE & (1 << 7)) != 0) {
		// If the PDE�s PS flag is 1, the PDE maps a 2-MByte page. The
		// final physical address is computed as follows:
		// � Bits 51:21 are from the PDE.
		// � Bits 20:0 are from the original va address.
		return (PDE & 0xFFFFFFFE00000) + (va & 0x1FFFFF);
	}

	//
	// PS bit was 0. That means that the PDE references a Page Table.
	// 
	auto PTE = g_pSpdfan->ReadPhysicalAddress<uint64_t>((PDE & 0xFFFFFFFFFF000) + Table * sizeof(ULONGLONG));

	if (PTE == 0)
		return 0;

	//
	// The PTE maps a 4-KByte page. The
	// final physical address is computed as follows:
	// � Bits 51:12 are from the PTE.
	// � Bits 11:0 are from the original va address.
	return (PTE & 0xFFFFFFFFFF000) + (va & 0xFFF);
}